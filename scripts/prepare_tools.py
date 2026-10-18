# Adapted from ESPGaluda2-PC ba85ac0, scripts/prepare_tools.py.
"""Restore pinned portable tools without changing PATH or machine installations."""
import argparse
import json
import os
from pathlib import Path
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
    else:
        with tarfile.open(archive) as stream:
            for item in stream.getmembers():
                validate_archive_member(item.name)
                if not item.isfile() and not item.isdir():
                    raise ValueError('Linked or special file in tool TAR archive')
            stream.extractall(destination, filter='data')
    return destination / package['archiveRoot'] if package['archiveRoot'] else destination


def prepare(cache=None):
    lock = json.loads((ROOT / 'tools/packages.lock.json').read_text(encoding='utf-8'))
    state_path = ROOT / 'tools/state/installed.json'
    previous = json.loads(state_path.read_text(encoding='utf-8')) if state_path.exists() else {}
    records = []
    for package in lock['packages']:
        target = ROOT / 'tools/apps' / package['id']
        reject_link(target)
        if target.exists():
            entry = target / package['entry']
            actual = digest(entry)
            known_receipt = next((item for item in previous.get('packages', []) if item['id'] == package['id']), None)
            trusted_download = known_receipt and known_receipt.get('archiveSha256') == package['sha256']
            if actual != package['entrySha256'] and not (trusted_download and actual == known_receipt['entrySha256']):
                raise ValueError(f'Existing {package["id"]} differs; preserve and inspect it')
            archive_sha = known_receipt.get('archiveSha256') if known_receipt else None
        else:
            seed = cache / package['id'] if cache else None
            if seed and (seed / package['entry']).is_file() and digest(seed / package['entry']) == package['entrySha256']:
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
    prepare(parser.parse_args().cache_from)
