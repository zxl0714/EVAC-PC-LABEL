"""Certify the PS2 runtime, reviewed generated inputs and matching symbols."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid

from analysis_paths import private_output
from build_receipt import signature
from project import ROOT, digest, reject_link, write_json_new
from ps2_game import generate_all
from ps2_codegen import tree_identity
from ps2_gs_sources import digests as gs_digests
from ps4_sources import locked_dependencies, verify_checkout
from toolchain import tool_path, tool_snapshot

BUILD = ROOT / 'out/ps2-tools'
RECEIPT = ROOT / 'out/ps2/build-receipt.json'
OUTPUTS = ('espgaluda.exe', 'espgaluda.pdb', 'SDL3.dll', 'SDL3.pdb')


def configuration(build=BUILD):
    values = {}
    for line in (Path(build) / 'CMakeCache.txt').read_text(encoding='utf-8').splitlines():
        if line and not line.startswith(('#', '//')) and ':' in line and '=' in line:
            key, value = line.split('=', 1)
            values[key.split(':', 1)[0]] = value
    if (values.get('PS2_GAME_SYMBOLS') != 'ON' or values.get('PS2_RUNTIME_THINLTO') != 'ON'
            or values.get('CMAKE_BUILD_TYPE') != 'Release'):
        raise ValueError('PS2 certification requires the Release runtime with full game symbols and ThinLTO')
    if values.get('PS2_EXPERIMENTAL_LATE_INPUT') == 'ON':
        raise ValueError('Private frame-order candidates are not release inputs')
    for key, tool in (('CMAKE_C_COMPILER', 'clang-cl'), ('CMAKE_CXX_COMPILER', 'clang-cl'),
                      ('CMAKE_MAKE_PROGRAM', 'ninja')):
        if Path(values.get(key, '')).resolve() != tool_path(tool, ROOT).resolve():
            raise ValueError('PS2 build does not use the pinned toolchain: ' + key)
    return values


def generation(values, refresh=False):
    output = Path(values['PS2_GAME_GENERATED']).parent.parent
    identities = {}
    inputs = generate_all(output, source_identities=identities, refresh=refresh)
    expected = dict(PS2_GAME_GENERATED=inputs['boot'], PS2_ARCADE_GENERATED=inputs['arcade'],
                    PS2_ARRANGE_GENERATED=inputs['arrange'], PS2_STAGE_MODULES_MANIFEST=inputs['stages'],
                    PS2_MODE_EXTRAS_MANIFEST=inputs['extras'])
    if any(Path(values[key]).resolve() != Path(path).resolve() for key, path in expected.items()):
        raise ValueError('PS2 build mixes different generated input sets')
    directories = sorted(output.glob('module-*/base-generated')) + sorted(output.glob('module-*/extra-generated'))
    if len(directories) != 31:
        raise ValueError('PS2 build requires all reviewed base and extension modules')
    return dict(plan=digest(output / 'plan.json'), stages=digest(Path(inputs['stages'])),
                extras=digest(Path(inputs['extras'])), sources=[identities[str(p)] for p in directories])


def snapshot(refresh=False):
    dependencies = locked_dependencies(ROOT, 'ps2')
    for item in dependencies:
        verify_checkout(ROOT / '.deps/ps2' / item['name'], item)
    names = subprocess.check_output(['git', '--no-optional-locks', '-C', str(ROOT), 'ls-files', '-z',
                                     '--cached', '--others', '--exclude-standard']).split(b'\0')
    explicit = {'scripts/Build-Common.ps1', 'scripts/project.py', 'scripts/build_receipt.py',
                'scripts/toolchain.py', 'scripts/generate_catalogue.py', 'scripts/product.py',
                'scripts/ps4_sources.py', 'tools/packages.lock.json',
                'tools/python-tools.lock.txt', 'tools/ps2-source.lock.json', 'config/default.ini',
                'tools/latency/latency_probe.h', 'tools/latency/test_pad.h',
                'games/catalogue.json', 'scripts/stage_ps2.py'}
    files = {}
    for raw in sorted(set(names)):
        name = raw.decode('utf-8')
        if name and (name in explicit or name.startswith(('backends/ps2/', 'third_party/ps2/',
                'tools/ps2/', 'tools/ps2-gs/', 'common/', 'games/espgaluda/', 'backends/ps4/input/'))
                or name.startswith(('scripts/ps2_', 'scripts/Build-Ps2', 'scripts/Build-Espgaluda',
                                    'tests/ps2_', 'tests/test_ps2_'))
                or name in ('backends/ps4/community/protocol.cpp','backends/ps4/community/protocol.h',
                            'backends/ps4/config/ini_sections.h')
                or (name.startswith('games/') and name.endswith('/data.json'))):
            path = ROOT / name
            reject_link(path)
            files[name] = digest(path)
    values = configuration()
    view = Path(values['PS2_GS_SOURCE'])
    declared = json.loads((view / 'collection-source.json').read_text(encoding='utf-8'))
    if declared.get('files') != gs_digests(view):
        raise ValueError('PS2 graphics build view differs from its pinned sources')
    flags={key:values.get(key,'') for key in ('CMAKE_C_FLAGS_RELEASE','CMAKE_CXX_FLAGS_RELEASE',
                                             'CMAKE_EXE_LINKER_FLAGS','CMAKE_SHARED_LINKER_FLAGS')}
    state = dict(files=files, dependencies=dependencies, tools=tool_snapshot(ROOT), flags=flags,
                 generated=generation(values,refresh=refresh), graphics=declared, buildType='Release', game='espgaluda')
    state['sha256'] = hashlib.sha256(json.dumps(state, sort_keys=True).encode()).hexdigest()
    return state


def completed_outputs():
    outputs = []
    for stem, suffix in (('espgaluda', '.exe'), ('SDL3', '.dll')):
        binary, symbols = BUILD / 'bin' / (stem + suffix), BUILD / 'bin' / (stem + '.pdb')
        pe = subprocess.check_output([str(tool_path('readobj', ROOT)), '--coff-debug-directory', str(binary)]).decode()
        pdb = subprocess.check_output([str(tool_path('pdbutil', ROOT)), 'dump', '-summary', str(symbols)]).decode()
        identity = signature(pe)
        if identity != signature(pdb, pdb=True):
            raise ValueError('PS2 binary/PDB identity differs: ' + stem)
        for path in (binary, symbols):
            reject_link(path)
            outputs.append(dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                                sha256=digest(path), symbols=identity))
    return outputs


def verify():
    record = json.loads(RECEIPT.read_text(encoding='utf-8'))
    if record.get('format') != 1 or record.get('backend') != 'ps2' or record.get('games') != ['espgaluda']:
        raise ValueError('Invalid PS2 build receipt identity')
    if record['sourceSnapshot'] != snapshot():
        raise ValueError('PS2 inputs changed; rebuild before staging')
    expected = {'out/ps2-tools/bin/' + name for name in OUTPUTS}
    if len(record['outputs']) != len(expected) or {item['path'] for item in record['outputs']} != expected:
        raise ValueError('Incomplete PS2 build receipt')
    for item in record['outputs']:
        path = ROOT / item['path']
        reject_link(path)
        if path.stat().st_size != item['bytes'] or digest(path) != item['sha256']:
            raise ValueError('PS2 output differs: ' + item['path'])
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['begin', 'finish', 'verify'])
    parser.add_argument('--state', type=Path)
    parser.add_argument('--stage',action='store_true',help='Stage directly after successful finish verification')
    args = parser.parse_args()
    if args.stage and args.action!='finish':
        parser.error('--stage requires finish')
    if args.action == 'verify':
        record = verify()
    elif args.action == 'begin':
        if not args.state:
            parser.error('--state required')
        write_json_new(private_output(args.state), snapshot(refresh=True))
        return
    else:
        if not args.state:
            parser.error('--state required')
        before = json.loads(args.state.read_text(encoding='utf-8'))
        after = snapshot()
        if before != after:
            raise ValueError('PS2 inputs changed during build')
        record = dict(format=1, backend='ps2', games=['espgaluda'], sourceSnapshot=after,
            outputs=completed_outputs(), sourceCommit=subprocess.check_output(
                ['git', '-C', str(ROOT), 'rev-parse', 'HEAD']).decode().strip(),
            sourceDirty=bool(subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'])))
        RECEIPT.parent.mkdir(parents=True, exist_ok=True)
        temporary = RECEIPT.with_name('receipt-' + uuid.uuid4().hex + '.json')
        write_json_new(temporary, record)
        os.replace(temporary, RECEIPT)
        if args.stage:
            from stage_ps2 import _stage_verified
            print(json.dumps(_stage_verified(record),indent=2))
    print('PS2 runtime receipt verified; two matching binary/PDB pairs.')


if __name__ == '__main__':
    main()
