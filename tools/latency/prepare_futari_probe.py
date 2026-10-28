"""Generate an isolated instrumentation overlay without editing player sources."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_game import execution_manifest, targets
from project import digest, write_json_new
from generate_catalogue import load


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Measurement source injection anchor changed: ' + old[:80])
    return text.replace(old, new, 1)


def prepare(folder, sites_path):
    folder.mkdir(parents=True, exist_ok=False)
    source = ROOT / 'games/futari/recomp'
    manifest = tomllib.loads((source / 'futari.toml').read_text(encoding='utf-8'))
    # Keep all original declarations/registry; codegen only the measured overlay.
    game = next(g for g in load(ROOT / 'games/catalogue.json') if g['id'] == 'futari')
    records = targets(manifest, game)
    generated = folder / 'generated'
    originals = {}
    sites = {}
    for line in sites_path.read_text(encoding='ascii').splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        module, address, opcode, identifier, *registers = line.split()
        sites.setdefault(module, []).append((int(address, 16), int(identifier), list(map(int, registers))))
    for module in ('default', 'reco_abnormal'):
        program = ROOT / 'game-data/futari' / records[module]['program_path']
        expected = next(f for f in game['files'] if f['path'] == records[module]['program_path'])
        if digest(program) != expected['sha256']:
            raise ValueError('Unexpected original module: ' + module)
        hooks, bodies = [], []
        for address, identifier, registers in sites[module]:
            name = f'FutariProbeSite{identifier}'
            hooks.append(f'[[midasm_hook]]\naddress = 0x{address:08X}\nname = "{name}"\nregisters = ["context", "memory"]\n')
            values = [f'ctx.r{r}.u64' if r < 32 else 'ctx.lr' if r == 32 else 'ctx.ctr.u64' for r in registers]
            bodies.append(f'void {name}(PPCContext& ctx, std::uint8_t*) {{\n'
                f'  FutariProbeRecord(10, {identifier}, 0x{address:08X}, ' + ', '.join(values) + ');\n' +
                ('  FutariProbeSwap(ctx.r3.u32);\n' if identifier == 6 else '') + '}\n')
        declaration = folder / f'{module}-probe.toml'
        declaration.write_text('\n'.join(hooks), encoding='ascii')
        records[module]['includes'] = list(records[module].get('includes', [])) + [str(declaration)]
        (folder / f'{module}-probe.cpp').write_text('#include "futari_probe_exports.h"\n' + '\n'.join(bodies), encoding='ascii')
        originals[records[module]['program_path']] = expected['sha256']
    # execution_manifest intentionally rejects external includes. Produce its
    # normal output first, then append only our two controlled local paths.
    for module in ('default', 'reco_abnormal'):
        records[module]['includes'].pop()
    execution = execution_manifest(game, records, ROOT / 'game-data/futari', generated, source)
    for module in ('default', 'reco_abnormal'):
        original = 'includes = [' + ', '.join(json.dumps(str(source / p).replace('\\', '/')) for p in records[module]['includes']) + ']'
        patched = original[:-1] + ', ' + json.dumps(str(folder / f'{module}-probe.toml').replace('\\', '/')) + ']'
        execution = replace_once(execution, original, patched)
    config = folder / 'execution.toml'
    config.write_text(execution, encoding='utf-8')
    executable = ROOT / '.deps/sdk-install/bin/rexglue.exe'
    with (folder / 'codegen.log').open('xb') as log:
        subprocess.run([str(executable), 'codegen', str(config), '--target', 'reco_abnormal'],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    shutil.copy2(ROOT / 'generated/futari/default/module_registry.cpp', generated / 'default/module_registry.cpp')
    host_path = ROOT / 'games/futari/host.cpp'
    text = host_path.read_text(encoding='utf-8')
    text = '#include "futari_probe_exports.h"\n' + text
    text = replace_once(text, 'void OnGameSetup() override {', 'void OnGameSetup() override {\n        FutariProbeInitialize();')
    text = replace_once(text, 'void OnGameStop() override {', 'void OnGameStop() override {\n        FutariProbeFlush();')
    (folder / 'host.cpp').write_text(text, encoding='utf-8')
    input_path = ROOT / 'backends/xbox360/input/configured_input.cpp'
    text = input_path.read_text(encoding='utf-8')
    text = '#include "futari_probe_exports.h"\n' + text
    text = replace_once(text, 'X_RESULT GetState(uint32_t user, X_INPUT_STATE* output) override {',
        'X_RESULT GetState(uint32_t user, X_INPUT_STATE* output) override {\n'
        '        if (user == 0 && output && FutariProbeInput(output)) return X_ERROR_SUCCESS;')
    (folder / 'configured_input.cpp').write_text(text, encoding='utf-8')
    write_json_new(folder / 'preparation.json', dict(original_modules=originals,
        source_inputs={str(p.relative_to(ROOT)): digest(p) for p in (host_path, input_path, sites_path)},
        generated_code_edited=False, production_sources_edited=False))
    print(folder)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--sites', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.directory.resolve(), args.sites.resolve())
