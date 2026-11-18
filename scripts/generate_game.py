"""Generate selected game code from local, verified data; never execute guest code."""
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import re
import subprocess
import tomllib

from build_receipt import verify_sdk
from generate_catalogue import load, relative
from project import ROOT, digest, reject_link, write_json_new
from sdoj_title_update import TITLE_UPDATE, patch_inputs, stage_programs, title_update_configs

SUPPORTED_GAMES = ('espgaluda2', 'futari', 'sdoj', 'guwange')


def verify_data(game, data):
    data = Path(data).resolve(strict=True)
    for name in game.get('forbiddenFiles', []):
        path = data / relative(name)
        if path.exists() or path.is_symlink() or path.is_junction():
            raise ValueError('Unsupported companion: ' + name)
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
    game_id = game['id']
    if game_id not in SUPPORTED_GAMES or manifest['project']['name'] != game_id:
        raise ValueError('Unexpected code generation project')
    if manifest['project']['game_root'] != '../../../game-data/' + game_id:
        raise ValueError('Unexpected declarative game root')
    programs = {item['path'].casefold(): item['path'] for item in game['files'] if item['program']}
    if 'default.xex' not in programs:
        raise ValueError('Missing resident program identity')
    records = {'default': dict(manifest['entrypoint'], program_path=programs['default.xex'])}
    for item in manifest['modules']:
        guest = relative(item['guest_path'])
        name = Path(guest).stem
        if (guest.casefold() not in programs or name in records or
                not re.fullmatch('[A-Za-z][A-Za-z0-9_]*', name) or
                Path(guest).suffix != {'sdoj': '.bin', 'guwange': '.dll'}.get(game_id, '.xex')):
            raise ValueError('Expected unique declared program overlays')
        records[name] = dict(item, program_path=programs[guest.casefold()])
    if {item['program_path'] for item in records.values()} != set(programs.values()):
        raise ValueError('Manifest does not cover precisely the supported programs')
    for name, item in records.items():
        if set(item) - {'file_path', 'out_directory_path', 'includes', 'guest_path', 'program_path'}:
            raise ValueError('Put callback/hook declarations in includes; do not silently drop them')
        if (item['file_path'] != '../../../game-data/' + game_id + '/' + item['program_path'] or
                item['out_directory_path'] != '../../../generated/' + game_id + '/' + name):
            raise ValueError('Unexpected declarative generation paths')
        includes = item.get('includes', [])
        if (not isinstance(includes, list) or not all(isinstance(path, str) for path in includes) or
                len(set(includes)) != len(includes)):
            raise ValueError('Expected unique include paths')
        for include in includes:
            relative(include)
    return records


def execution_manifest(game, records, data, generated, source):
    quoted = lambda value: json.dumps(str(value).replace('\\', '/'), ensure_ascii=False)
    lines = ['[project]', 'name = ' + quoted(game['id']), 'sdk_version = "0.9.0"',
             'game_root = ' + quoted(data), '']
    for name, item in records.items():
        lines += ['[entrypoint]' if name == 'default' else '[[modules]]']
        if name != 'default':
            lines.append('guest_path = ' + quoted(item['guest_path']))
        lines += ['file_path = ' + quoted(data / item['program_path']),
                  'out_directory_path = ' + quoted(generated / name)]
        if item.get('includes'):
            paths = []
            for include in item['includes']:
                path = source / relative(include)
                reject_link(path)
                paths.append(quoted(path))
            lines.append('includes = [' + ', '.join(paths) + ']')
        lines.append('')
    return '\n'.join(lines)


def registry_source(records, game_id, library_prefix=None):
    if game_id not in SUPPORTED_GAMES:
        raise ValueError('Unsupported registry game')
    lines = ['// Generated module registry metadata; no translated program bodies.',
             '#include <rex/system/kernel_state.h>', '#include <rex/filesystem.h>',
             '#include "common/catalogue.h"',
             'void RegisterRecompiledModules(rex::system::KernelState* kernel_state) {',
             f'  const auto root=rex::filesystem::GetExecutableFolder().parent_path()/"games/{game_id}";']
    library_prefix = library_prefix or game_id
    for name, item in records.items():
        if name != 'default':
            guest = relative(item['guest_path'])
            lines += ['  {', f'    const auto library=cave::Utf8(root/"{library_prefix}_{name}.dll");',
                      f'    kernel_state->RegisterRecompiledModule("{Path(guest).name}","{guest}",library.c_str());', '  }']
    return '\n'.join(lines + ['}', ''])


def verify_generated_branches(generated, records):
    """The code generator can exit zero after emitting a branch failure trap."""
    failures = []
    for name in records:
        for path in (Path(generated) / name).glob('*.cpp'):
            reject_link(path)
            for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
                if 'REX_FATAL("Unresolved' in line or 'ERROR: conditional branch' in line:
                    failures.append(f'{name}/{path.name}:{number}')
    if failures:
        raise ValueError('Unresolved generated branches; fix source declarations: ' + ', '.join(failures[:16]))


def generate(game_id, data, regenerate=False, title_update=None):
    game = next(game for game in load(ROOT / 'games/catalogue.json') if game['id'] == game_id)
    if game_id not in SUPPORTED_GAMES:
        raise ValueError('Unsupported game generator')
    data = Path(data).resolve(strict=True)
    generated = ROOT / 'generated' / game_id
    reject_link(generated)
    if generated.is_relative_to(data) or data.is_relative_to(generated):
        raise ValueError('Generated outputs must stay separate from game inputs')
    verify_sdk()
    base_inputs = verify_data(game, data)
    update = game['titleUpdate']
    if title_update is not None and not update:
        raise ValueError('This game does not declare a title update')
    patches = patch_inputs(data, update, title_update) if update else {}
    before = base_inputs + [
        {key: entry[key] for key in ('path', 'bytes', 'sha256')} for entry in update['patches']
    ] if update else base_inputs
    source = ROOT / 'games' / game_id / 'recomp'
    manifest = source / (game_id + '.toml')
    records = targets(tomllib.loads(manifest.read_text(encoding='utf-8')), game)
    executable = ROOT / '.deps/sdk-install/bin/rexglue.exe'
    inputs = [Path(__file__).resolve(), executable, executable.parent / 'rexruntime.dll',
              ROOT / 'games/catalogue.json', ROOT / 'games' / game_id / 'data.json',
              *sorted(source.glob('*.toml'))]
    if update:
        inputs.append(Path(__file__).resolve().with_name('sdoj_title_update.py'))
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

    base_data = stage_programs(records, data, report / 'base-data', {}) if update else data
    editions = [(base_data, generated, source, records, game_id)]
    if update:
        configs = report / 'tu101-configs'
        translated = title_update_configs(records, source, configs)
        patched_data = stage_programs(records, data, report / 'tu101-data', patches)
        editions.append((patched_data, generated / TITLE_UPDATE, configs, translated, game_id + '_' + TITLE_UPDATE))

    if stamp_path.exists():
        stamp_path.rename(report / 'previous-generation.json')
    folders = [directory / name for _, directory, _, declared, _ in editions for name in declared]
    old_outputs = {}
    for folder in folders:
        reject_link(folder)
        if folder.is_dir():
            for path in folder.iterdir():
                reject_link(path)
                if path.is_file() and path.suffix in ('.cpp', '.h', '.cmake'):
                    old_outputs[path] = (digest(path), path.stat().st_atime_ns, path.stat().st_mtime_ns)
    results = []
    try:
        for edition_data, directory, config_source, declared, prefix in editions:
            execution = report / (prefix + '.toml')
            execution.write_text(execution_manifest(game, declared, edition_data, directory, config_source),
                                 encoding='utf-8', newline='\n')
            for name in declared:
                if name == 'default':
                    continue
                print(f'Generating {prefix}: default + {name}; one overlay per process', flush=True)
                with (report / (prefix + '-' + name + '.log')).open('xb') as log:
                    result = subprocess.run([str(executable), 'codegen', str(execution), '--target', name],
                        cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, timeout=600)
                results.append(dict(edition=prefix, module=name, returnCode=result.returncode))
                if result.returncode:
                    raise RuntimeError(f'Generation failed for {prefix}/{name}; see {report}')
            for name in declared:
                if not (directory / name / 'sources.cmake').is_file():
                    raise ValueError('Incomplete generated target: ' + name)
            verify_generated_branches(directory, declared)
            # Consolidate metadata from the isolated runs without modifying translated bodies.
            (directory / 'default/module_registry.cpp').write_text(
                registry_source(declared, game_id, prefix), encoding='utf-8', newline='\n')
            (directory / 'default/dll_targets.cmake').write_text(
                f'# Targets are maintained in games/{game_id}.\n', encoding='utf-8')
        outputs = []
        for folder in folders:
            for path in sorted(folder.iterdir()):
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
        unchanged = base_inputs == verify_data(game, data)
        if update:
            unchanged = unchanged and patches == patch_inputs(data, update, title_update)
        write_json_new(report / 'run.json', dict(format=1, game=game_id, inputs=hashes, modules=results,
            gameDataUnchanged=unchanged, guestExecution=False))
    if not unchanged:
        raise ValueError('Game inputs changed while generating')
    write_json_new(stamp_path, dict(format=1, game=game_id, inputs=hashes, gameInputs=before,
        modules=list(records), outputs=outputs, gameDataUnchanged=True, guestExecution=False))
    print(json.dumps(dict(modules=len(records), generatedFiles=len(outputs), report=str(report)), indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', choices=SUPPORTED_GAMES, required=True)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--regenerate', action='store_true')
    parser.add_argument('--title-update', type=Path, help='Separate verified disc TU 1.01 file for SDOJ')
    args = parser.parse_args()
    generate(args.game, args.data_dir, args.regenerate, args.title_update)
