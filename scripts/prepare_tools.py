# Adapted from ESPGaluda2-PC ba85ac0, scripts/prepare_tools.py.
"""Restore pinned portable tools without changing PATH or machine installations."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import tarfile
import tempfile
import urllib.request
import zipfile

from project import ROOT, digest, reject_link, write_json_new


def validate_archive_member(name):
    path = Path(name)
    if path.anchor or '..' in path.parts or ':' in name or chr(92) in name:
        raise ValueError(f'Unsafe tool archive member: {name}')


def extract(package, archive, destination):
    reject_link(destination)
    if destination.exists():
        raise FileExistsError(destination)
    if archive.name.endswith('.zip'):
        with zipfile.ZipFile(archive) as stream:
            for item in stream.infolist():
                validate_archive_member(item.orig_filename)
                if (item.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError('Linked file in ZIP tools archive')
            stream.extractall(destination)
    elif archive.name.endswith('.tar.zst'):
        try:
            import zstandard
        except ImportError as error:
            raise RuntimeError('Run Prepare-Tools.ps1 -Ps4 to prepare the locked zstd decoder') from error
        with archive.open('rb') as packed, tempfile.TemporaryFile() as unpacked:
            with zstandard.ZstdDecompressor(max_window_size=2**31).stream_reader(packed) as reader:
                shutil.copyfileobj(reader, unpacked, 1024 * 1024)
            unpacked.seek(0)
            with tarfile.open(fileobj=unpacked) as stream:
                extract_tar(stream, destination)
    else:
        with tarfile.open(archive) as stream:
            extract_tar(stream, destination)
    return destination / package['archiveRoot'] if package['archiveRoot'] else destination


def extract_tar(stream, destination):
    for item in stream.getmembers():
        validate_archive_member(item.name)
        if not item.isfile() and not item.isdir():
            raise ValueError('Linked or special file in tool TAR archive')
    stream.extractall(destination, filter='data')


def verify_required_files(package, directory):
    for name, expected in package.get('requiredFiles', {}).items():
        validate_archive_member(name)
        path = directory / name
        reject_link(path)
        if digest(path) != expected:
            raise ValueError(f'Pinned tool component differs: {name}')


def prepare(cache=None, ps4=False):
    lock_name = 'ps4-tools.lock.json' if ps4 else 'packages.lock.json'
    lock = json.loads((ROOT / 'tools' / lock_name).read_text(encoding='utf-8'))
    state_path = ROOT / ('tools/state/installed-ps4.json' if ps4 else 'tools/state/installed.json')
    if lock.get('format') != 1 or lock.get('platform') != 'windows-x64':
        raise ValueError('Unsupported tool lock')
    names = set()
    for package in lock['packages']:
        if (not re.fullmatch('[a-z0-9-]+', package['id']) or package['id'] in names
                or not re.fullmatch('[a-f0-9]{64}', package['sha256'])
                or not re.fullmatch('[a-f0-9]{64}', package['entrySha256'])
                or not package['url'].startswith('https://')):
            raise ValueError('Invalid pinned tool package')
        names.add(package['id'])
        for name in (package['entry'], package['archiveRoot'], package.get('cacheDirectory', package['id'])):
            validate_archive_member(name)
    previous = json.loads(state_path.read_text(encoding='utf-8')) if state_path.exists() else {}
    records = []
    for package in lock['packages']:
        target = ROOT / 'tools/apps' / package['id']
        reject_link(target)
        if target.exists():
            entry = target / package['entry']
            actual = digest(entry)
            known_receipt = next((item for item in previous.get('packages', []) if item['id'] == package['id']), None)
            if actual != package['entrySha256']:
                raise ValueError(f'Existing {package["id"]} differs; preserve and inspect it')
            archive_sha = known_receipt.get('archiveSha256') if known_receipt else None
        else:
            candidates = [cache / package['id'], cache / package.get('cacheDirectory', package['id'])] if cache else []
            seed = next((path for path in candidates if (path / package['entry']).is_file()
                         and digest(path / package['entry']) == package['entrySha256']), None)
            if seed:
                reject_link(seed)
                verify_required_files(package, seed)
                for current, directories, names in os.walk(seed, followlinks=False):
                    for name in directories + names:
                        reject_link(Path(current) / name)
                print(f'Copying pinned {package["id"]} from local cache', flush=True)
                shutil.copytree(seed, target)
                if digest(target / package['entry']) != package['entrySha256']:
                    raise ValueError('Tool changed during cache copy')
                archive_sha = None
            else:
                archive = ROOT / 'tools/downloads' / package['url'].split('/')[-1].replace('%2B', '+')
                reject_link(archive)
                archive.parent.mkdir(parents=True, exist_ok=True)
                if not archive.exists():
                    temporary = archive.with_name(archive.name + '.part')
                    with temporary.open('xb') as output, urllib.request.urlopen(package['url']) as source:
                        shutil.copyfileobj(source, output, 1024 * 1024)
                    if digest(temporary) != package['sha256']:
                        raise ValueError(f'Archive hash failed; preserve {temporary}')
                    temporary.rename(archive)
                if digest(archive) != package['sha256']:
                    raise ValueError(f'Cached archive differs: {archive}')
                staging_root = ROOT / '.local/tool-extraction'
                staging_root.mkdir(parents=True, exist_ok=True)
                staging = Path(tempfile.mkdtemp(prefix=package['id'] + '-', dir=staging_root)) / 'unpacked'
                expanded = extract(package, archive, staging)
                if not (expanded / package['entry']).is_file():
                    raise ValueError('Expected executable not found in verified tool archive')
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(expanded, target)
                archive_sha = package['sha256']
        if digest(target / package['entry']) != package['entrySha256']:
            raise ValueError('Tool executable differs from its fixed entry hash')
        verify_required_files(package, target)
        records.append(dict(id=package['id'], version=package['version'],
            entry='tools/apps/' + package['id'] + '/' + package['entry'],
            entrySha256=digest(target / package['entry']), archiveSha256=archive_sha))
    state = dict(format=1, packages=records)
    if previous and previous != state:
        raise ValueError('Tool receipt changed; preserve existing receipt and investigate')
    if not state_path.exists():
        write_json_new(state_path, state)
    print(json.dumps(state, indent=2), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache-from', type=Path)
    parser.add_argument('--ps4', action='store_true')
    args = parser.parse_args()
    prepare(args.cache_from, ps4=args.ps4)
