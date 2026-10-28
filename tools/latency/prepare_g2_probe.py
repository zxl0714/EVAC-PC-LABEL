"""Generate a private G2 timing overlay with original-opcode-pinned sites."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_game import execution_manifest, targets
from generate_catalogue import load
from project import digest, write_json_new
from prepare_futari_probe import replace_once
from ppc_functions import functions

# Four registers per site, identical native/JIT observation payloads. These
# identify original synchronous ownership; they do not claim a pixel response.
SITES = {
    'default': [
        (1, 0x820ED8BC, (3, 30, 31, 32)),
        (2, 0x820ED8C4, (3, 30, 31, 32)),
        (3, 0x820CFDA8, (3, 31, 29, 32)),
        (4, 0x820BB274, (3, 31, 4, 33)),
        (5, 0x820BB278, (3, 31, 29, 32)),
        (6, 0x82170F84, (3, 4, 31, 32)),
        (7, 0x821712CC, (11, 31, 30, 32)),
        (8, 0x821713A0, (3, 31, 4, 32)),
        (9, 0x821713A8, (3, 31, 4, 32)),
        (15, 0x820F3E40, (3, 4, 31, 32)),
        (16, 0x820912B8, (3, 31, 4, 32)),
        (17, 0x820912BC, (3, 31, 4, 32)),
        (18, 0x820F418C, (3, 4, 31, 32)),
        (19, 0x820F43D0, (3, 28, 31, 32)),
    ],
    'dat12': [
        (11, 0x8814AC90, (3, 21, 31, 33)),
        (12, 0x88022C88, (3, 31, 4, 32)),
        (13, 0x88022C8C, (3, 31, 4, 32)),
        (14, 0x88022CB0, (3, 31, 4, 32)),
    ],
}


def prepare(folder, census_root):
    private = ROOT / '.local/input-latency-v080-r1'
    if not folder.is_relative_to(private):
        raise ValueError('Private overlay directory required')
    source = ROOT / 'games/espgaluda2/recomp'
    manifest = tomllib.loads((source / 'espgaluda2.toml').read_text(encoding='utf-8'))
    game = next(g for g in load(ROOT / 'games/catalogue.json') if g['id'] == 'espgaluda2')
    records = targets(manifest, game)
    pinned = []
    sections_hashes = {}
    for module, specs in SITES.items():
        program = ROOT / 'game-data/espgaluda2' / records[module]['program_path']
        expected = next(f for f in game['files'] if f['path'] == program.name)['sha256']
        if digest(program) != expected:
            raise ValueError('Original G2 module identity changed')
        census_dir = census_root / module
        census = json.loads((census_dir / 'census.json').read_text())
        if census['guestExecution'] or census['gpuInitialized'] or census['audioInitialized']:
            raise ValueError('Expected tool-mode original section dump')
        sections = []
        for s in census['sections']:
            if s['inImage']:
                path = census_dir / s['dump']
                sections.append((s['address'], path.read_bytes()))
                sections_hashes[str(path)] = digest(path)
        decoded = {pc: asm for _, insns, _ in functions('espgaluda2', module) for pc, asm in insns}
        for identifier, pc, regs in specs:
            if pc not in decoded:
                raise ValueError(f'Not an instruction boundary: {pc:08X}')
            matches = [struct.unpack_from('>I', data, pc - start)[0] for start, data in sections
                       if start <= pc and pc + 4 <= start + len(data)]
            if len(matches) != 1:
                raise ValueError('Ambiguous original instruction section')
            op = matches[0]
            asm = decoded[pc]
            if asm.startswith('bl 0x'):
                displacement = op & 0x03fffffc
                if displacement & 0x02000000:
                    displacement -= 0x04000000
                target = (displacement if op & 2 else pc + displacement) & 0xffffffff
                if op >> 26 != 18 or not op & 1 or target != int(asm[5:], 16):
                    raise ValueError('Original branch target mismatch')
                if identifier == 6:
                    vd_swap, = [i for i in census['imports'] if i['name'] == 'VdSwap']
                    if target != vd_swap['thunkAddress']:
                        raise ValueError('Guest submit site is not the original VdSwap import')
            pinned.append(dict(module=module, id=identifier, address=pc, opcode=op,
                               registers=regs, asm=asm, input_sha256=expected))
    folder.mkdir(parents=True, exist_ok=False)
    generated = folder / 'generated'
    sites_path = folder / 'guest-sites.txt'
    sites_path.write_text(''.join(f"{s['module']} {s['address']:08X} {s['opcode']:08X} {s['id']} " +
                         ' '.join(map(str, s['registers'])) + '\n' for s in pinned), encoding='ascii')
    execution = execution_manifest(game, records, ROOT / 'game-data/espgaluda2', generated, source)
    for module in SITES:
        hooks, bodies = [], []
        for s in (s for s in pinned if s['module'] == module):
            name = f"G2ProbeSite{s['id']}"
            hooks.append(f"[[midasm_hook]]\naddress = 0x{s['address']:08X}\nname = \"{name}\"\nregisters = [\"context\", \"memory\"]\n")
            vals = [f'ctx.r{r}.u64' if r < 32 else 'ctx.lr' if r == 32 else 'ctx.ctr.u64' for r in s['registers']]
            bodies.append(f'void {name}(PPCContext& ctx, std::uint8_t*) {{\n' +
                f"  FutariProbeRecord(10, {s['id']}, 0x{s['address']:08X}, " + ', '.join(vals) + ');\n' +
                ('  FutariProbeSwap(ctx.r3.u32);\n' if s['id'] == 6 else '') + '}\n')
        declaration = folder / f'{module}-probe.toml'
        declaration.write_text('\n'.join(hooks), encoding='ascii')
        (folder / f'{module}-probe.cpp').write_text('#include "futari_probe_exports.h"\n' + '\n'.join(bodies), encoding='ascii')
        original = 'includes = [' + ', '.join(json.dumps(str(source / p).replace('\\', '/')) for p in records[module]['includes']) + ']'
        execution = replace_once(execution, original,
            original[:-1] + ', ' + json.dumps(str(declaration).replace('\\', '/')) + ']')
    config = folder / 'execution.toml'
    config.write_text(execution, encoding='utf-8')
    with (folder / 'codegen.log').open('xb') as log:
        subprocess.run([str(ROOT / '.deps/sdk-install/bin/rexglue.exe'), 'codegen', str(config),
                        '--target', 'dat12'], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    shutil.copy2(ROOT / 'generated/espgaluda2/default/module_registry.cpp', generated / 'default/module_registry.cpp')
    host_path = ROOT / 'games/espgaluda2/host.cpp'
    host = '#include "futari_probe_exports.h"\n' + host_path.read_text(encoding='utf-8')
    host = replace_once(host, 'private:\n', 'private:\n    void OnGameSetup() override { FutariProbeInitialize(); }\n')
    host = replace_once(host, 'void OnGameStop() override {', 'void OnGameStop() override {\n        FutariProbeFlush();')
    (folder / 'host.cpp').write_text(host, encoding='utf-8')
    input_path = ROOT / 'backends/xbox360/input/configured_input.cpp'
    text = '#include "futari_probe_exports.h"\n' + input_path.read_text(encoding='utf-8')
    text = replace_once(text, 'X_RESULT GetState(uint32_t user, X_INPUT_STATE* output) override {',
        'X_RESULT GetState(uint32_t user, X_INPUT_STATE* output) override {\n'
        '        if (user == 0 && output && FutariProbeInput(output)) return X_ERROR_SUCCESS;')
    (folder / 'configured_input.cpp').write_text(text, encoding='utf-8')
    changed = {}
    for module in SITES:
        changed[module] = [p.name for p in (generated / module).glob('*.cpp')
                           if digest(p) != digest(ROOT / 'generated/espgaluda2' / module / p.name)]
    write_json_new(folder / 'preparation.json', dict(sites=pinned, original_sections=sections_hashes,
        source_inputs={str(p): digest(p) for p in (host_path, input_path, Path(__file__))},
        changed_cpp=changed, generated_code_edited=False, production_sources_edited=False))
    print(json.dumps(dict(directory=str(folder), changed_cpp=changed), indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--census', type=Path, required=True)
    a = p.parse_args()
    prepare(a.directory.resolve(), a.census.resolve())
