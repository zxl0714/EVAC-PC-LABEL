"""Restore isolated, pinned PS4 build dependencies without game data or an old project."""
import argparse
import json
from pathlib import Path
import re
import subprocess

from project import ROOT, reject_link


def git(path, *args):
    return subprocess.check_output(['git', '--no-optional-locks', '-C', str(path), *map(str, args)],
                                   stderr=subprocess.PIPE).decode('utf-8').strip()


def locked_dependencies(root=ROOT):
    path = Path(root) / 'tools/ps4-source.lock.json'
    reject_link(path)
    lock = json.loads(path.read_text(encoding='utf-8'))
    if lock.get('format') != 1 or not isinstance(lock.get('dependencies'), list) or not lock['dependencies']:
        raise ValueError('Invalid PS4 dependency lock')
    seen = set()
    for item in lock['dependencies']:
        if (not isinstance(item, dict) or set(item) != {'name', 'revision', 'url'}
                or not isinstance(item['name'], str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', item['name'])
                or item['name'].casefold() in seen or not isinstance(item['revision'], str)
                or not re.fullmatch(r'[a-f0-9]{40}', item['revision']) or not isinstance(item['url'], str)
                or not re.fullmatch(r'https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\.git', item['url'])):
            raise ValueError('Invalid or duplicate PS4 dependency')
        seen.add(item['name'].casefold())
    return lock['dependencies']


def verify_checkout(path, item):
    path = Path(path).absolute()
    reject_link(path)
    reject_link(path / '.git')
    if not (path / '.git').is_dir():
        raise ValueError(f'Expected an independent dependency checkout: {path}')
    common = Path(git(path, 'rev-parse', '--path-format=absolute', '--git-common-dir')).resolve()
    if common != (path / '.git').resolve():
        raise ValueError('Dependency Git objects must be self-contained')
    if (git(path, 'rev-parse', 'HEAD') != item['revision']
            or git(path, 'remote', 'get-url', 'origin') != item['url']
            or git(path, 'status', '--porcelain', '--untracked-files=all')):
        raise ValueError(f'Dependency differs or is dirty; existing files preserved: {path}')
    alternate = path / '.git/objects/info/alternates'
    reject_link(alternate)
    if alternate.exists():
        raise ValueError('Dependency has external Git object alternates')


def restore(root=ROOT, cache=None, verify_only=False):
    root = Path(root).absolute()
    reject_link(root)
    dependencies = locked_dependencies(root)
    for item in dependencies:
        target = root / '.deps/ps4' / item['name']
        reject_link(target)
        if target.exists():
            verify_checkout(target, item)
            continue
        if verify_only:
            raise FileNotFoundError(target)
        seed = Path(cache).absolute() / item['name'] if cache else None
        if seed and seed.exists():
            # Cache reuse is explicit and verified; never borrow its Git object
            # storage, remote, config or working files through a link.
            verify_checkout(seed, item)
        target.parent.mkdir(parents=True, exist_ok=True)
        print(f'Restoring PS4 {item["name"]} at {item["revision"][:12]}', flush=True)
        if seed and seed.exists():
            command = ['git', 'clone', '--no-local', '--no-hardlinks', '--no-checkout', '--depth', '1',
                       '--', str(seed), str(target)]
        else:
            command = ['git', 'clone', '--filter=blob:none', '--no-checkout', '--', item['url'], str(target)]
        subprocess.run(command, check=True)
        if not seed or not seed.exists():
            git(target, 'fetch', '--depth=1', 'origin', item['revision'])
        git(target, 'checkout', '--detach', item['revision'])
        git(target, 'remote', 'set-url', 'origin', item['url'])
        verify_checkout(target, item)
    return dict(verifiedDependencies=len(dependencies), directory='.deps/ps4', gameDataIncluded=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['restore', 'verify'])
    parser.add_argument('--cache-from', type=Path, help='Optional cache with dependency-name directories')
    args = parser.parse_args()
    print(json.dumps(restore(cache=args.cache_from, verify_only=args.action == 'verify'), indent=2))
