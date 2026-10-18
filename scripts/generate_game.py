"""Generate selected game code from local, verified data; never execute guest code."""
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import tomllib

from build_receipt import verify_sdk
from generate_catalogue import load, relative
from project import ROOT, digest, reject_link, write_json_new


def verify_data(game, data):
    data = Path(data).resolve(strict=True)
    result = []
    for item in game['files']:
        path = (data / relative(item['path'])).resolve()
        if not path.is_relative_to(data) or not path.is_file():
            raise ValueError('Missing or redirected data: ' + item['path'])
        if path.stat().st_size != item['bytes'] or digest(path) != item['sha256']:
            raise ValueError('Unsupported data: ' + item['path'])
        result.append(dict(path=item['path'], bytes=item['bytes'], sha256=item['sha256']))
    return result


def targets(manifest, game):
    if manifest['project']['name'] != game['id']:
        raise ValueError('Unexpected code generation project')
    records = {'default': manifest['entrypoint']}
    for item in manifest['modules']:
        guest = item['guest_path']
        name = Path(guest).stem
        if guest != name + '.xex' or name in records or not name.startswith('dat') or not name[3:].isdigit():
            raise ValueError('Expected unique root-level overlays')
        records[name] = item
    if {name + '.xex' for name in records} != {item['path'] for item in game['files'] if item['program']}:
        raise ValueError('Manifest does not cover precisely the supported programs')
    for name, item in records.items():
        if (item['file_path'] != '../../../game-data/espgaluda2/' + name + '.xex' or
                item['out_directory_path'] != '../../../generated/espgaluda2/' + name):
            raise ValueError('Unexpected declarative generation paths')
    return records


def execution_manifest(game, records, data, generated, source):
    quoted = lambda value: json.dumps(str(value).replace('\\', '/'), ensure_ascii=False)
    lines = ['[project]', 'name = ' + quoted(game['id']), 'sdk_version = "0.9.0"',
             'game_root = ' + quoted(data), '']
    for name, item in records.items():
        lines += ['[entrypoint]' if name == 'default' else '[[modules]]']
        if name != 'default':
            lines.append('guest_path = ' + quoted(item['guest_path']))
        lines += ['file_path = ' + quoted(data / (name + '.xex')),
                  'out_directory_path = ' + quoted(generated / name)]
        if name == 'default':
            paths = []
            for include in item['includes']:
                path = source / relative(include)
                reject_link(path)
                paths.append(quoted(path))
            lines.append('includes = [' + ', '.join(paths) + ']')
        lines.append('')
    return '\n'.join(lines)


def registry_source(names):
    lines = ['// Generated module registry metadata; no translated program bodies.',
             '#include <rex/system/kernel_state.h>', '#include <rex/filesystem.h>',
             '#include "common/catalogue.h"',
             'void RegisterRecompiledModules(rex::system::KernelState* kernel_state) {',
             '  const auto root=rex::filesystem::GetExecutableFolder().parent_path()/"games/espgaluda2";']
    for name in names:
        if name != 'default':
            lines += ['  {', f'    const auto library=cave::Utf8(root/"espgaluda2_{name}.dll");',
                      f'    kernel_state->RegisterRecompiledModule("{name}.xex","{name}.xex",library.c_str());', '  }']
    return '\n'.join(lines + ['}', ''])


def generate(game_id, data, regenerate=False):
    game = next(game for game in load(ROOT / 'games/catalogue.json') if game['id'] == game_id)
    if game_id != 'espgaluda2':
        raise ValueError('This generator currently supports only ESPGaluda II')
    data = Path(data).resolve(strict=True)
    generated = ROOT / 'generated' / game_id
    reject_link(generated)
    if generated.is_relative_to(data) or data.is_relative_to(generated):
        raise ValueError('Generated outputs must stay separate from game inputs')
    verify_sdk()
    before = verify_data(game, data)
    source = ROOT / 'games' / game_id / 'recomp'
    manifest = source / (game_id + '.toml')
    records = targets(tomllib.loads(manifest.read_text(encoding='utf-8')), game)
    executable = ROOT / '.deps/sdk-install/bin/rexglue.exe'
    inputs = [Path(__file__).resolve(), executable, executable.parent / 'rexruntime.dll',
              ROOT / 'games/catalogue.json', ROOT / 'games' / game_id / 'data.json',
              *sorted(source.glob('*.toml'))]
    hashes = {path.relative_to(ROOT).as_posix(): digest(path) for path in inputs}
    stamp_path = generated / 'generation.json'
    if not regenerate and stamp_path.exists():
        previous = json.loads(stamp_path.read_text(encoding='utf-8'))
        if (previous['inputs'] == hashes and previous['gameInputs'] == before and all(
                (ROOT / item['path']).is_file() and digest(ROOT / item['path']) == item['sha256']
                for item in previous['outputs'])):
            print('Verified generation inputs and outputs unchanged; keeping generated files.')
            return
    stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    report = ROOT / '.local/codegen' / game_id / stamp
    reject_link(report)
    report.mkdir(parents=True, exist_ok=False)
    if stamp_path.exists():
        stamp_path.rename(report / 'previous-generation.json')
    execution = report / 'execution.toml'
    execution.write_text(execution_manifest(game, records, data, generated, source), encoding='utf-8', newline='\n')
    old_outputs = {}
    for name in records:
        folder = generated / name
        reject_link(folder)
        if folder.is_dir():
            for path in folder.iterdir():
                reject_link(path)
                if path.is_file() and path.suffix in ('.cpp', '.h', '.cmake'):
                    old_outputs[path] = (digest(path), path.stat().st_atime_ns, path.stat().st_mtime_ns)
    results = []
    try:
        for name in records:
            if name == 'default':
                continue
            print(f'Generating {game_id}: default + {name}; one overlay per process', flush=True)
            with (report / (name + '.log')).open('xb') as log:
                result = subprocess.run([str(executable), 'codegen', str(execution), '--target', name],
                    cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, timeout=600)
            results.append(dict(module=name, returnCode=result.returncode))
            if result.returncode:
                raise RuntimeError(f'Generation failed for {name}; see {report}')
        for name in records:
            if not (generated / name / 'sources.cmake').is_file():
                raise ValueError('Incomplete generated target: ' + name)
        # Only registry metadata is consolidated after the isolated codegen runs.
        # Translated guest function bodies are never patched by this script.
        (generated / 'default/module_registry.cpp').write_text(registry_source(records), encoding='utf-8', newline='\n')
        (generated / 'default/dll_targets.cmake').write_text(
            '# Targets are maintained in games/espgaluda2/overlays.cmake.\n', encoding='utf-8')
        outputs = []
        for name in records:
            for path in sorted((generated / name).iterdir()):
                reject_link(path)
                if path.is_file() and path.suffix in ('.cpp', '.h', '.cmake'):
                    current_hash = digest(path)
                    outputs.append(dict(path=path.relative_to(ROOT).as_posix(), sha256=current_hash))
                    old = old_outputs.get(path)
                    if old and old[0] == current_hash:
                        os.utime(path, ns=(old[1], old[2]))
        if hashes != {path.relative_to(ROOT).as_posix(): digest(path) for path in inputs}:
            raise ValueError('Generation inputs changed while generating')
        verify_sdk()
    finally:
        unchanged = before == verify_data(game, data)
        write_json_new(report / 'run.json', dict(format=1, game=game_id, inputs=hashes, modules=results,
            gameDataUnchanged=unchanged, guestExecution=False))
    write_json_new(stamp_path, dict(format=1, game=game_id, inputs=hashes, gameInputs=before,
        modules=list(records), outputs=outputs, gameDataUnchanged=True, guestExecution=False))
    print(json.dumps(dict(modules=len(records), generatedFiles=len(outputs), report=str(report)), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', choices=['espgaluda2'], required=True)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--regenerate', action='store_true')
    args = parser.parse_args()
    generate(args.game, args.data_dir, args.regenerate)
