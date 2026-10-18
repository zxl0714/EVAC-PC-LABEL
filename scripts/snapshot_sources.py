"""Create a verified, non-overwriting backup of Git-selected working sources."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess


def git(root, *args):
    return subprocess.check_output(
        ['git', '--no-optional-locks', '-C', str(root), *args])


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def checked_path(root, relative):
    parts = PurePosixPath(relative).parts
    if not parts or any(part in ('..', '.') for part in parts) or PurePosixPath(relative).is_absolute():
        raise ValueError(f'Unsafe source path: {relative}')
    path = root.joinpath(*parts)
    if not path.resolve().is_relative_to(root):
        raise ValueError(f'Source escapes repository: {relative}')
    for current in [path, *path.parents]:
        if current.is_symlink() or current.is_junction():
            raise ValueError(f'Linked source: {relative}')
        if current == root:
            break
    return path


def snapshot(root, destination):
    root, destination = Path(root).resolve(), Path(destination).absolute()
    if destination.exists() or destination.resolve().is_relative_to(root) or root.is_relative_to(destination.resolve()):
        raise ValueError('Backup must be a new directory outside the source repository')
    if destination.parent.resolve() != destination.parent.absolute():
        raise ValueError('Backup parent must not redirect through links')
    if root != Path(git(root, 'rev-parse', '--show-toplevel').decode().strip()).resolve():
        raise ValueError('Source must be a repository root')
    head = git(root, 'rev-parse', 'HEAD').decode().strip()
    state = git(root, 'status', '--porcelain=v1', '-z', '--untracked-files=all')
    names = sorted({name.decode('utf-8') for name in git(
        root, 'ls-files', '-z', '--cached', '--others', '--exclude-standard').split(b'\0') if name})
    links = {}
    for raw in git(root, 'ls-files', '--stage', '-z').split(b'\0'):
        if raw.startswith(b'160000 '):
            fields, name = raw.decode('utf-8').split('\t', 1)
            links[name] = fields.split()[1]
    records, deleted = [], []
    for name in names:
        if name in links:
            continue
        path = checked_path(root, name)
        if not path.exists():
            deleted.append(name)
            continue
        if not path.is_file():
            raise ValueError(f'Non-regular source: {name}')
        records.append(dict(path=name, bytes=path.stat().st_size, sha256=digest(path)))
    destination.mkdir(parents=True, exist_ok=False)
    for record in records:
        source = checked_path(root, record['path'])
        copied = destination / 'source' / record['path']
        copied.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, copied)
        if digest(copied) != record['sha256'] or digest(source) != record['sha256']:
            raise ValueError('Source changed or copy mismatch; preserve partial backup')
    with (destination / 'working-tree.patch').open('xb') as output:
        output.write(git(root, 'diff', '--binary', 'HEAD', '--'))
    if head != git(root, 'rev-parse', 'HEAD').decode().strip() or state != git(
            root, 'status', '--porcelain=v1', '-z', '--untracked-files=all'):
        raise ValueError('Repository changed during snapshot; preserve partial backup')
    for record in records:
        if digest(checked_path(root, record['path'])) != record['sha256']:
            raise ValueError('Source changed during snapshot; preserve partial backup')
    metadata = dict(format=1, createdUtc=datetime.now(timezone.utc).isoformat(),
                    repository=str(root), commit=head, status=state.decode('utf-8').split('\0'),
                    gitlinks=links, deleted=deleted, files=records,
                    totalBytes=sum(record['bytes'] for record in records),
                    excludes='Ignored data, installed tools and dependency checkout contents; gitlinks pinned separately')
    with (destination / 'snapshot.json').open('x', encoding='utf-8', newline='\n') as output:
        json.dump(metadata, output, ensure_ascii=False, indent=2)
        output.write('\n')
    verify(destination)
    return metadata


def verify(destination):
    destination = Path(destination).resolve()
    metadata = json.loads((destination / 'snapshot.json').read_text(encoding='utf-8'))
    for record in metadata['files']:
        path = checked_path(destination / 'source', record['path'])
        if path.stat().st_size != record['bytes'] or digest(path) != record['sha256']:
            raise ValueError(f'Backup mismatch: {record["path"]}')
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path, nargs='?')
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    if args.verify:
        result = verify(args.source)
    else:
        if args.destination is None:
            parser.error('destination is required for a new snapshot')
        result = snapshot(args.source, args.destination)
    print(json.dumps(dict(commit=result['commit'], files=len(result['files']),
                          bytes=result['totalBytes'], verified=True), indent=2))


if __name__ == '__main__':
    main()
