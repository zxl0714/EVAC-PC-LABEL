"""Prepare and stage the small, pinned player PKG extraction dependency."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile
import urllib.request
import zipfile

from project import ROOT, digest, reject_link
from prepare_tools import validate_archive_member


def lock(root=ROOT):
    return json.loads((root / 'tools/ps4-extract.lock.json').read_text(encoding='utf-8'))


def verify(root=ROOT):
    spec = lock(root)
    directory = root / 'tools/apps' / spec['directory']
    for name, sha256 in spec['files'].items():
        validate_archive_member(name)
        file = directory / name
        reject_link(file)
        if not file.is_file() or digest(file) != sha256:
            raise ValueError('Run py -3.12 scripts/ps4_extract_tools.py prepare; missing/changed ' + name)
    return directory


def prepare(root=ROOT):
    spec = lock(root)
    directory = root / 'tools/apps' / spec['directory']
    reject_link(directory)
    if directory.exists():
        return verify(root)
    archive = root / 'tools/downloads' / ('PkgTool-' + spec['version'] + '.zip')
    reject_link(archive)
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        pending = archive.with_suffix('.zip.part')
        with pending.open('xb') as output, urllib.request.urlopen(spec['url'], timeout=60) as source:
            shutil.copyfileobj(source, output)
        if digest(pending) != spec['sha256']:
            raise ValueError('PkgTool archive checksum differs; partial download retained')
        pending.rename(archive)
    if digest(archive) != spec['sha256']:
        raise ValueError('PkgTool archive checksum differs')
    directory.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pkgtool-', dir=directory.parent) as temporary:
        expanded = Path(temporary)
        with zipfile.ZipFile(archive) as packed:
            for entry in packed.infolist():
                validate_archive_member(entry.orig_filename)
                if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError('Linked file in PkgTool archive')
            for name, sha256 in spec['files'].items():
                packed.extract(name, expanded)
                if digest(expanded / name) != sha256:
                    raise ValueError('PkgTool archive component differs: ' + name)
        # Only the required player files are installed; no upstream GUI or tests.
        expanded.rename(directory)
    return verify(root)


def player_sources(root=ROOT):
    spec = lock(root)
    return {'tools/pkgtool/' + name: root / 'tools/apps' / spec['directory'] / name
            for name in spec['files']} | {
        'tools/pkgtool/GPL-3.0.txt': root / 'config/licenses/GPL-3.0.txt',
        'tools/pkgtool/SOURCE.txt': root / 'config/pkgtool-source.txt',
        'tools/README.txt': root / 'config/README.extract-ps4.txt'}


def stage(build, destination, root=ROOT):
    verify(root)
    destination = Path(destination)
    reject_link(destination)
    sources = {**player_sources(root),
               'tools/Extract-PS4.exe': Path(build) / 'bin/Extract-PS4.exe',
               'tools/Extract-Ps4Pkg.ps1': root / 'scripts/Extract-Ps4Pkg.ps1',
               'tools/README.txt': root / 'config/README.extract-ps4.txt',
               'tools/pkgtool/SOURCE.txt': root / 'config/pkgtool-source.txt'}
    for relative, source in sources.items():
        reject_link(source)
        target = destination / relative
        reject_link(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists() and digest(target) == digest(source):
            continue
        shutil.copy2(source, target)
    return destination / 'tools/Extract-PS4.exe'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'verify', 'stage'))
    parser.add_argument('--build', type=Path, default=ROOT / 'out/common')
    parser.add_argument('--destination', type=Path, default=ROOT / 'out/ps4-extract-player')
    args = parser.parse_args()
    print(stage(args.build, args.destination) if args.command == 'stage' else globals()[args.command]())


if __name__ == '__main__':
    main()
