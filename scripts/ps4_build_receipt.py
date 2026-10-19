"""Bind the ROM-free PS4 build to its own sources, dependencies, tools and symbols."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid

from analysis_paths import private_output
from build_receipt import signature
from generate_catalogue import relative
from project import ROOT, digest, reject_link, write_json_new
from ps4_sources import locked_dependencies, verify_checkout

OUTPUTS = {
    'out/ps4-native': ('KetsuiNativeProbe',),
    'out/ps4-gpu': ('KetsuiGpuCoreSmoke', 'KetsuiGpuRuntimeSmoke', 'KetsuiGpuCompileMetricsSmoke', 'KetsuiGpuRuntime'),
}
RECEIPT = ROOT / 'out/ps4/build-receipt.json'


def upstream(root=ROOT):
    verified = 0
    for component, prefix in (('gcn', ''), ('gpu_core', 'upstream')):
        vendor = Path(root) / 'third_party/ps4' / component
        inventory = json.loads((vendor / 'upstream-files.json').read_text(encoding='utf-8'))
        changes = {}
        for item in inventory.get('localChanges', []):
            name = relative(item['path'])
            if name in changes:
                raise ValueError('Duplicate maintained PS4 source change')
            changes[name] = item['sha256']
        names = set()
        for item in inventory['files']:
            name = relative(item['path'])
            if name in names:
                raise ValueError('Duplicate PS4 source inventory path')
            names.add(name)
            path = vendor / prefix / name
            reject_link(path)
            if digest(path) != changes.get(name, item['sha256']):
                raise ValueError('PS4 upstream source differs; record the maintained change: ' + name)
            verified += 1
        if set(changes) - names:
            raise ValueError('PS4 change is not declared in upstream inventory')
    return verified


def snapshot():
    upstream()
    dependencies = locked_dependencies()
    for item in dependencies:
        verify_checkout(ROOT / '.deps/ps4' / item['name'], item)
    raw = subprocess.check_output(['git', '--no-optional-locks', '-C', str(ROOT),
                                   'ls-files', '-z', '--cached', '--others', '--exclude-standard'])
    explicit = {'scripts/Build-Ps4.ps1', 'scripts/Build.ps1', 'scripts/Test.ps1',
                'scripts/Build-Common.ps1', 'scripts/ps4_build_receipt.py',
                'scripts/Prepare-Tools.ps1', 'scripts/prepare_tools.py', 'scripts/ps4/run_native_checks.py',
                'scripts/ps4_sources.py', 'scripts/build_receipt.py', 'scripts/project.py',
                'tools/ps4-source.lock.json', 'tools/ps4-tools.lock.json', 'tools/python-ps4.lock.txt',
                'tools/packages.lock.json', 'config/default.ini', 'games/catalogue.json', 'scripts/generate_catalogue.py'}
    files = {}
    for name in sorted({value.decode('utf-8') for value in raw.split(b'\0') if value}):
        if name in explicit or name.startswith(('backends/ps4/', 'third_party/ps4/', 'common/')) or (name.startswith('games/') and name.endswith('/data.json')):
            path = ROOT / name
            reject_link(path)
            files[name] = digest(path)
    lock = json.loads((ROOT / 'tools/ps4-tools.lock.json').read_text())
    package, = lock['packages']
    compiler = ROOT / 'tools/apps' / package['id']
    expected = {package['entry']: package['entrySha256'], **package['requiredFiles']}
    tools = {}
    for name, sha in expected.items():
        path = compiler / relative(name)
        reject_link(path)
        actual = digest(path)
        if actual != sha:
            raise ValueError('PS4 tool changed: ' + name)
        tools[path.relative_to(ROOT).as_posix()] = actual
    for name in ('cmake/bin/cmake.exe', 'ninja/ninja.exe', 'llvm/bin/llvm-readobj.exe', 'llvm/bin/llvm-pdbutil.exe'):
        path = ROOT / 'tools/apps' / name
        reject_link(path)
        tools[path.relative_to(ROOT).as_posix()] = digest(path)
    state = dict(files=files, dependencies=dependencies, tools=tools,
                 buildType='RelWithDebInfo', gameDataRequired=False)
    state['sha256'] = hashlib.sha256(json.dumps(state, sort_keys=True).encode()).hexdigest()
    return state


def completed_outputs():
    pairs = []
    for directory, names in OUTPUTS.items():
        for name in names:
            binary = ROOT / directory / (name + ('.dll' if name == 'KetsuiGpuRuntime' else '.exe'))
            symbols = binary.with_suffix('.pdb')
            pe_text = subprocess.check_output([str(ROOT / 'tools/apps/llvm/bin/llvm-readobj.exe'),
                                              '--coff-debug-directory', str(binary)]).decode('utf-8')
            pdb_text = subprocess.check_output([str(ROOT / 'tools/apps/llvm/bin/llvm-pdbutil.exe'),
                                               'dump', '-summary', str(symbols)]).decode('utf-8')
            identity = signature(pe_text)
            if identity != signature(pdb_text, pdb=True):
                raise ValueError('PS4 binary/PDB identity differs: ' + name)
            for path in (binary, symbols):
                pairs.append(dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                                  sha256=digest(path), symbols=identity))
    return pairs


def verify():
    record = json.loads(RECEIPT.read_text())
    if type(record.get('format')) is not int or record['format'] != 1 or record.get('backend') != 'ps4':
        raise ValueError('Invalid PS4 build receipt identity')
    if record['sourceSnapshot'] != snapshot():
        raise ValueError('PS4 sources/tools changed; rebuild before using outputs')
    expected = {f'{folder}/{name}.{extension}' for folder, names in OUTPUTS.items() for name in names
                for extension in (('dll', 'pdb') if name == 'KetsuiGpuRuntime' else ('exe', 'pdb'))}
    if {item['path'] for item in record['outputs']} != expected or len(record['outputs']) != len(expected):
        raise ValueError('Incomplete PS4 build receipt')
    for item in record['outputs']:
        path = ROOT / relative(item['path'])
        reject_link(path)
        if digest(path) != item['sha256']:
            raise ValueError('PS4 output differs: ' + item['path'])
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['begin', 'finish', 'verify'])
    parser.add_argument('--state', type=Path)
    args = parser.parse_args()
    if args.action == 'verify':
        record = verify()
    elif args.action == 'begin':
        if not args.state:
            parser.error('--state required')
        write_json_new(private_output(args.state), snapshot())
        return
    else:
        if not args.state:
            parser.error('--state required')
        before = json.loads(args.state.read_text())
        after = snapshot()
        if before != after:
            raise ValueError('PS4 source/tool input changed during the build')
        record = dict(format=1, backend='ps4', sourceSnapshot=after, outputs=completed_outputs(),
                      sourceCommit=subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD']).decode().strip(),
                      sourceDirty=bool(subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'])))
        RECEIPT.parent.mkdir(parents=True, exist_ok=True)
        temporary = RECEIPT.with_name('receipt-' + uuid.uuid4().hex + '.json')
        write_json_new(temporary, record)
        os.replace(temporary, RECEIPT)
    print(f'PS4 source/tool/dependency receipt verified; {len(record["outputs"]) // 2} binary/PDB pairs.')


if __name__ == '__main__':
    main()
