"""Resolve and verify the one pinned toolchain used by every collection backend."""
import argparse
import json
from pathlib import Path
import re

from project import ROOT, digest, reject_link

TOOLS = {
    'clang': ('llvm', 'bin/clang.exe'),
    'clang++': ('llvm', 'bin/clang++.exe'),
    'clang-cl': ('llvm', 'bin/clang-cl.exe'),
    'lld-link': ('llvm', 'bin/lld-link.exe'),
    'readobj': ('llvm', 'bin/llvm-readobj.exe'),
    'pdbutil': ('llvm', 'bin/llvm-pdbutil.exe'),
    'cmake': ('cmake', 'bin/cmake.exe'),
    'ninja': ('ninja', 'ninja.exe'),
}


def safe_relative(name):
    path = Path(name)
    if not name or path.anchor or '..' in path.parts or ':' in name or '\\' in name:
        raise ValueError('Unsafe pinned tool path: ' + name)
    return name


def install_directory(package):
    name = package.get('installDirectory', package['id'])
    if not re.fullmatch(r'[a-z0-9][a-z0-9.-]*', name) or name in ('.', '..'):
        raise ValueError('Invalid tool installation directory')
    return name


def locked_packages(root=None):
    root = Path(root) if root is not None else ROOT
    lock = json.loads((root / 'tools/packages.lock.json').read_text(encoding='utf-8'))
    if lock.get('format') != 1 or lock.get('platform') != 'windows-x64':
        raise ValueError('Unsupported tool lock')
    names, directories = set(), set()
    for package in lock['packages']:
        if (not re.fullmatch(r'[a-z0-9-]+', package['id']) or package['id'] in names
                or not re.fullmatch(r'[a-f0-9]{64}', package['sha256'])
                or not re.fullmatch(r'[a-f0-9]{64}', package['entrySha256'])
                or not package['url'].startswith('https://')):
            raise ValueError('Invalid pinned tool package')
        directory = install_directory(package)
        if directory in directories:
            raise ValueError('Duplicate tool installation directory')
        names.add(package['id']); directories.add(directory)
        safe_relative(package['entry'])
        for name in (package['archiveRoot'], package.get('cacheDirectory', package['id'])):
            if name:
                safe_relative(name)
        for name, expected in package.get('requiredFiles', {}).items():
            safe_relative(name)
            if not re.fullmatch(r'[a-f0-9]{64}', expected):
                raise ValueError('Invalid tool component digest')
        if package['entry'] in package.get('requiredFiles', {}) and package['requiredFiles'][package['entry']] != package['entrySha256']:
            raise ValueError('Conflicting tool entry digests')
    return lock['packages']


def component(package, name, root):
    expected = {package['entry']: package['entrySha256'], **package.get('requiredFiles', {})}
    if name not in expected:
        raise ValueError('Tool component is not pinned: ' + name)
    path = Path(root) / 'tools/apps' / install_directory(package) / safe_relative(name)
    reject_link(path)
    if digest(path) != expected[name]:
        raise ValueError('Pinned tool component differs: ' + name)
    return path


def tool_path(name, root=None):
    root = Path(root) if root is not None else ROOT
    package_id, relative = TOOLS[name]
    package, = (item for item in locked_packages(root) if item['id'] == package_id)
    return component(package, relative, root)


def tool_snapshot(root=None):
    root = Path(root) if root is not None else ROOT
    result = {}
    for package in locked_packages(root):
        for name in {package['entry'], *package.get('requiredFiles', {})}:
            path = component(package, name, root)
            result[path.relative_to(root).as_posix()] = digest(path)
    return result


def receipt_path(root=None):
    root = Path(root) if root is not None else ROOT
    return root / 'tools/state' / ('toolchain-' + digest(root / 'tools/packages.lock.json')[:16] + '.json')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['path', 'verify'])
    parser.add_argument('name', choices=TOOLS, nargs='?')
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    if args.action == 'path':
        if not args.name:
            parser.error('path requires a tool name')
        print(tool_path(args.name, args.root))
    else:
        print(json.dumps(tool_snapshot(args.root), indent=2))
