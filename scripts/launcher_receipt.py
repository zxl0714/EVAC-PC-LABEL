"""Certify launcher/inspection outputs against their ROM-free build inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid

from build_receipt import signature
from project import ROOT, digest, reject_link, write_json_new
from toolchain import tool_path, tool_snapshot

RECEIPT = ROOT / 'out/launcher/build-receipt.json'


def snapshot():
    raw = subprocess.check_output(['git', '--no-optional-locks', '-C', str(ROOT),
                                   'ls-files', '-z', '--cached', '--others', '--exclude-standard'])
    names = {name.decode() for name in raw.split(b'\0') if name}
    explicit = {'CMakeLists.txt', 'games/catalogue.json', 'config/default.ini', 'tools/packages.lock.json',
                'scripts/Build.ps1', 'scripts/Build-Common.ps1', 'scripts/generate_catalogue.py',
                'scripts/generate_artwork.py', 'scripts/launcher_receipt.py', 'scripts/toolchain.py'}
    files = {name: digest(ROOT / name) for name in sorted(names) if name in explicit or
             name.startswith(('common/', 'launcher/')) or (name.startswith('games/') and name.endswith('/data.json'))}
    tools = tool_snapshot(ROOT)
    state = dict(files=files, tools=tools)
    state['sha256'] = hashlib.sha256(json.dumps(state, sort_keys=True).encode()).hexdigest()
    return state


def commit():
    return subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD']).decode().strip()


def finish(state_path, build):
    before = json.loads(state_path.read_text())
    if before['sourceCommit'] != commit() or before['sourceSnapshot'] != snapshot():
        raise ValueError('Launcher inputs changed during build')
    build = build.resolve(strict=True)
    if not build.is_relative_to(ROOT / 'out'):
        raise ValueError('Launcher build must be inside collection/out')
    outputs = []
    readobj, pdbutil = tool_path('readobj', ROOT), tool_path('pdbutil', ROOT)
    for name in ('CAVE-PC-Collection', 'cave_inspect'):
        binary, symbols = build / 'bin' / (name + '.exe'), build / 'bin' / (name + '.pdb')
        pe = subprocess.check_output([str(readobj),
                                      '--coff-debug-directory', str(binary)]).decode()
        pdb = subprocess.check_output([str(pdbutil),
                                       'dump', '-summary', str(symbols)]).decode()
        identity = signature(pe)
        if identity != signature(pdb, pdb=True):
            raise ValueError('Launcher or inspector PDB does not match')
        for path in (binary, symbols):
            reject_link(path)
            outputs.append(dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                                sha256=digest(path), codeView=identity))
    for extension in ('exe', 'pdb'):
        if digest(ROOT / ('CAVE-PC-Collection.' + extension)) != digest(build / 'bin' / ('CAVE-PC-Collection.' + extension)):
            raise ValueError('Root launcher does not match this build')
    record = dict(format=1, backend='launcher', **before, outputs=outputs)
    pending = RECEIPT.with_name('receipt-' + uuid.uuid4().hex + '.json')
    write_json_new(pending, record); os.replace(pending, RECEIPT)
    return record


def verify():
    record = json.loads(RECEIPT.read_text())
    if record.get('format') != 1 or record.get('backend') != 'launcher' or record['sourceSnapshot'] != snapshot():
        raise ValueError('Launcher source/tool inputs changed; rebuild')
    if len(record['outputs']) != 4 or {Path(item['path']).name for item in record['outputs']} != {
            'CAVE-PC-Collection.exe', 'CAVE-PC-Collection.pdb', 'cave_inspect.exe', 'cave_inspect.pdb'}:
        raise ValueError('Incomplete launcher receipt')
    for item in record['outputs']:
        path = ROOT / item['path']; reject_link(path)
        if not path.resolve().is_relative_to(ROOT / 'out') or digest(path) != item['sha256']:
            raise ValueError('Launcher output differs from build receipt')
        if path.name.startswith('CAVE-PC-Collection.') and digest(ROOT / path.name) != item['sha256']:
            raise ValueError('Root launcher differs from build receipt')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['begin', 'finish', 'verify'])
    parser.add_argument('--state', type=Path)
    parser.add_argument('--build', type=Path)
    args = parser.parse_args()
    if args.action == 'begin':
        if not args.state or not args.state.resolve().is_relative_to(ROOT / '.local'):
            parser.error('--state must be a new file below collection/.local')
        write_json_new(args.state, dict(sourceCommit=commit(), sourceSnapshot=snapshot()))
    elif args.action == 'finish':
        if not args.state or not args.build:
            parser.error('finish requires --state and --build')
        finish(args.state, args.build)
        print('Launcher/inspector receipt verified; 2 binary/PDB pairs.')
    else:
        verify(); print('Launcher/inspector receipt verified; 2 binary/PDB pairs.')
