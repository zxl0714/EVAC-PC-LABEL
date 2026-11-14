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
        '.psm1', '.psd1', '.html', '.rc', '.java'}
SECRET = re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----|'
                    rb'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}')
RUNTIME_ARTWORK = {
    'backends/ps4/artwork/x-arrange-184x40.png':
        '63d2b6995a805c840f2f6c93d4ce30ca8a7009b9c36cfbe213192f8a2a3c8777',
    'backends/ps4/artwork/x-arrange-264x56.png':
        'deb4bb75632dd6f6c02e304ff049284642361074d133d6a7b1f54b155a2c489f',
    'backends/ps4/artwork/ketsui-x-arrange-331x58.png':
        '2eecbbd06a0e2cb380656e4dcc75cc2414a762df7c6f46593db8b92158640875',
    'backends/ps4/artwork/ketsui-x-arrange-188x44.png':
        '99167cbc17ef480708f9d79d7013ce285a4159e846dbff53e8cf534b5f3e3c58',
    'backends/ps4/artwork/ketsui-x-arrange-171x18.png':
        '6d1e0d7edeb2a08a018ae16edcfde84c72f93a1ac0aad61accf2fb9f5f470dc3',
    'backends/ps4/artwork/ketsui-x-arrange-160x13.png':
        '9fa80d7621b6e2a8b39af564047084ff5516ef47dd373a50a76e010735b7329b',
}
DOC_SCREENSHOTS = {
    'docs/images/launcher.png':
        'eeb1d684791155508359bdbe465ea342481b8cfc07251ee24a9d1f01c57708bc',
    'docs/images/ketsui-x-arrange.jpg':
        'c92de91289b8fdcb3ffb3b295561c219362dec19021b0b71674859f307c525f4',
}


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
    from generate_artwork import artwork, branding
    art={entry['path'] for entry in [*artwork(root),*artwork(root,'covers')]}
    brand={entry['path'] for entry in branding(root)}
    from ps2_game import analysis_sources
    from ps2_ibara import analysis_sources as ibara_analysis_sources
    analysis={**analysis_sources(root), **ibara_analysis_sources(root)}
    count, total = 0, 0
    for name in names:
        if not name.isascii():
            raise ValueError(f'Source filenames must use ASCII: {name}')
        if name in actual_links:
            continue
        first = Path(name).parts[0].casefold()
        if (first in LOCAL or first.startswith('user-data') or name == 'config.ini'
                or name.startswith(('tools/apps/', 'tools/downloads/', 'tools/state/'))):
            raise ValueError(f'Local data in source selection: {name}')
        path = root / name
        reject_link(path)
        if name in RUNTIME_ARTWORK:
            if digest(path) != RUNTIME_ARTWORK[name]:
                raise ValueError(f'Runtime artwork differs from the reviewed image: {name}')
            count += 1
            total += path.stat().st_size
            continue
        if name in DOC_SCREENSHOTS:
            if digest(path) != DOC_SCREENSHOTS[name]:
                raise ValueError(f'Documentation screenshot differs from the reviewed image: {name}')
            count += 1
            total += path.stat().st_size
            continue
        if name in art or name in brand:
            count+=1
            total+=path.stat().st_size
            continue
        if name in analysis:
            if path.stat().st_size > 5*1024*1024 or SECRET.search(analysis[name]):
                raise ValueError(f'Invalid PS2 analysis metadata: {name}')
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
    return dict(files=count, bytes=total, submodules=len(actual_links), artworkFiles=len(art),
                brandingFiles=len(brand), gameDataIncluded=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['audit'])
    parser.parse_args()
    print(json.dumps(audit_source(), indent=2))
