"""Collection source boundary audit and shared preparation utilities."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LOCAL = {'.local', '.deps', 'game-data', 'prepared', 'generated', 'runtime', 'out',
         'build', 'releases', 'reports', 'logs', 'cache', 'source'}
TEXT = {'.py', '.ps1', '.md', '.txt', '.json', '.ini', '.cpp', '.h', '.hpp', '.c',
        '.cmake', '.toml', '.in', '.manifest', '.yml', '.yaml', '.bat', '.gitignore',
        '.s', '.inc', '.inja', '.glsl', '.hlsl', '.vert', '.frag', '.comp',
        '.psm1', '.psd1', '.html', '.rc'}
SECRET = re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----|'
                    rb'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def reject_link(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError(f'Linked source/output: {path.name}')
        if part == ROOT:
            break


def config_path(root=ROOT):
    """Read-only config lookup; relative game paths remain collection-relative."""
    root = Path(root)
    current = root / 'user-data/config.ini'
    reject_link(current)
    if current.exists():
        return current
    legacy = root / 'config.ini'
    reject_link(legacy)
    return legacy if legacy.exists() else current


def write_json_new(path, value):
    reject_link(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False)
        stream.write('\n')


def audit_source(root=ROOT):
    root = Path(root).resolve()
    raw = subprocess.check_output(['git', '--no-optional-locks', '-C', str(root),
                                  'ls-files', '-z', '--cached', '--others', '--exclude-standard'])
    names = sorted(set(p.decode('utf-8') for p in raw.split(b'\0') if p))
    # Only dependency roots pinned in both the lock and Git index may be skipped.
    # Their own source trees are restored as independent submodules, not vendored
    # recursively by a source/package scan.
    from sdk_sources import index_links, locked_modules
    actual_links = index_links(root)
    expected_links = {item['path']: item['commit'] for item in locked_modules(root)}
    if actual_links != expected_links:
        raise ValueError('SDK dependency index differs from its source lock')
    from generate_artwork import artwork
    art={entry['path'] for entry in artwork(root)}
    count, total = 0, 0
    for name in names:
        if name in actual_links:
            continue
        first = Path(name).parts[0].casefold()
        if (first in LOCAL or first.startswith('user-data') or name == 'config.ini'
                or name.startswith(('tools/apps/', 'tools/downloads/', 'tools/state/'))):
            raise ValueError(f'Local data in source selection: {name}')
        path = root / name
        reject_link(path)
        if name in art:
            count+=1
            total+=path.stat().st_size
            continue
        if (not path.is_file() or path.stat().st_size > 5 * 1024 * 1024 or
                (path.suffix.lower() not in TEXT and path.name not in
                 {'.gitignore', '.gitattributes', '.gitmodules', '.editorconfig',
                  '.clang-format', '.clang-tidy', 'LICENSE', 'COPYING', 'NOTICE', 'AUTHORS', 'makefile'})):
            raise ValueError(f'Unexpected source file: {name}')
        data = path.read_bytes()
        if data.startswith((b'MZ', b'XEX2', b'\x7fELF', b'CON ', b'LIVE', b'PIRS')) or SECRET.search(data):
            raise ValueError(f'Binary, game container or credential in source: {name}')
        data.decode('utf-8-sig')
        count += 1
        total += len(data)
    return dict(files=count, bytes=total, submodules=len(actual_links), artworkFiles=len(art), gameDataIncluded=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['audit'])
    parser.parse_args()
    print(json.dumps(audit_source(), indent=2))
