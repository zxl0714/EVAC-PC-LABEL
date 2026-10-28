"""Generate private Guwange hooks from the already opcode-pinned Xenia sites."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_catalogue import load
from generate_game import execution_manifest, targets
from prepare_futari_probe import replace_once
from project import digest, write_json_new


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xenia-fixture', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    private = ROOT / '.local/input-latency-v080-r1'
    source_fixture, destination = args.xenia_fixture.resolve(), args.destination.resolve()
    if not all(p.is_relative_to(private) for p in (source_fixture, destination)) or destination.exists():
        raise ValueError('Fresh private output and validated private sites required')
    stage = json.loads((source_fixture / 'probe-stage.json').read_text())
    if stage['guest_memory_modified'] or stage['game_policy_modified'] or stage['core_only']:
        raise ValueError('Complete unmodified original observer set required')
    for path, expected in stage['sections'].items():
        if digest(Path(path)) != expected:
            raise ValueError('Original static section changed')
    game = next(g for g in load(ROOT / 'games/catalogue.json') if g['id'] == 'guwange')
    source = ROOT / 'games/guwange/recomp'
    manifest = tomllib.loads((source / 'guwange.toml').read_text(encoding='utf-8'))
    records = targets(manifest, game)
    modules = ('default', 'Guwange01_x360')
    for module in modules:
        path = ROOT / 'game-data/guwange' / records[module]['program_path']
        expected, = {s['program_sha256'] for s in stage['sites'] if s['module'] == module}
        if digest(path) != expected:
            raise ValueError('Original Guwange program changed')
    destination.mkdir()
    generated = destination / 'generated'
    execution = execution_manifest(game, records, ROOT / 'game-data/guwange', generated, source)
    for module in modules:
        declarations, bodies = [], []
        for site in (s for s in stage['sites'] if s['module'] == module):
            name = f"GuwangeProbeSite{site['id']}"
            declarations.append(f"[[midasm_hook]]\naddress = 0x{site['address']:08X}\nname = \"{name}\"\nregisters = [\"context\", \"memory\"]\n")
            values = [f'ctx.r{r}.u64' if r < 32 else 'ctx.lr' if r == 32 else 'ctx.ctr.u64'
                      for r in site['registers']]
            bodies.append(f'void {name}(PPCContext& ctx, std::uint8_t*) {{\n' +
                f"  FutariProbeRecord(10, {site['id']}, 0x{site['address']:08X}, " + ', '.join(values) + ');\n' +
                ('  FutariProbeSwap(ctx.r3.u32);\n' if site['id'] == 19 else '') + '}\n')
        declaration = destination / (module + '-probe.toml')
        declaration.write_text('\n'.join(declarations), encoding='ascii')
        (destination / (module + '-probe.cpp')).write_text(
            '#include "futari_probe_exports.h"\n' + '\n'.join(bodies), encoding='ascii')
        original = 'includes = [' + ', '.join(json.dumps(str(source / p).replace('\\', '/'))
                                            for p in records[module]['includes']) + ']'
        execution = replace_once(execution, original,
            original[:-1] + ', ' + json.dumps(declaration.as_posix()) + ']')
    config = destination / 'execution.toml'
    config.write_text(execution, encoding='utf-8')
    with (destination / 'codegen.log').open('xb') as log:
        subprocess.run([str(ROOT / '.deps/sdk-install/bin/rexglue.exe'), 'codegen', str(config),
            '--target', 'Guwange01_x360'], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    shutil.copy2(ROOT / 'generated/guwange/default/module_registry.cpp', generated / 'default/module_registry.cpp')
    host_path, input_path = ROOT / 'games/guwange/host.cpp', ROOT / 'backends/xbox360/input/configured_input.cpp'
    host = '#include "futari_probe_exports.h"\n' + host_path.read_text(encoding='utf-8')
    host = replace_once(host, 'void OnGameSetup() override {',
        'void OnGameSetup() override {\n        FutariProbeInitialize();')
    host = replace_once(host, 'void OnGameStop() override {',
        'void OnGameStop() override {\n        FutariProbeFlush();')
    (destination / 'host.cpp').write_text(host, encoding='utf-8')
    text = '#include "futari_probe_exports.h"\n' + input_path.read_text(encoding='utf-8')
    text = replace_once(text, 'X_RESULT GetState(uint32_t user, X_INPUT_STATE* output) override {',
        'X_RESULT GetState(uint32_t user, X_INPUT_STATE* output) override {\n'
        '        if (user == 0 && output && FutariProbeInput(output)) return X_ERROR_SUCCESS;')
    (destination / 'configured_input.cpp').write_text(text, encoding='utf-8')
    # Reuse the verified build orchestrator, not generated game bodies. Its
    # guarded output/object redirection remains unchanged in this private copy.
    template = ROOT / 'tools/latency/rebuild_g2_probe.py'
    builder = template.read_text(encoding='utf-8').replace('espgaluda2', 'guwange')
    builder = builder.replace('guwange_dat12', 'guwange_Guwange01_x360').replace("'dat12'", "'Guwange01_x360'")
    builder = replace_once(builder, 'ROOT = Path(__file__).resolve().parents[2]',
        'ROOT = Path(' + repr(str(ROOT)) + ')\nsys.path.insert(0, str(ROOT / "tools/latency"))')
    builder_path = destination / 'rebuild.py'
    builder_path.write_text(builder, encoding='utf-8')
    changed = {module:[p.name for p in (generated / module).glob('*.cpp')
        if digest(p) != digest(ROOT / 'generated/guwange' / module / p.name)] for module in modules}
    write_json_new(destination / 'preparation.json', dict(game='guwange',sites=stage['sites'],
        changed_cpp=changed, generated_code_edited=False, production_sources_edited=False,
        source_inputs={str(p):digest(p) for p in (host_path,input_path,template,builder_path,Path(__file__))},
        original_sections=stage['sections'], xenia_stage_sha256=digest(source_fixture / 'probe-stage.json')))
    print(json.dumps(dict(directory=str(destination),changed_cpp=changed), indent=2))


if __name__ == '__main__':
    main()
