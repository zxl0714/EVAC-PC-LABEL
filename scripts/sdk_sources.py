"""Restore pinned SDK dependencies without relying on another project checkout."""
import argparse
import configparser
import json
from pathlib import Path, PurePosixPath
import re
import subprocess

from project import ROOT, reject_link


def git(repo, *arguments):
    return subprocess.check_output(['git', '--no-optional-locks', '-C', str(repo),
                                    *map(str, arguments)], stderr=subprocess.PIPE)


def safe_relative(value):
    if (not isinstance(value, str) or not value or '\\' in value or ':' in value or
            any(c in value for c in '\r\n\t\0') or value.startswith('/') or
            any(p in ('', '.', '..', '.git') for p in value.split('/'))):
        raise ValueError(f'Unsafe dependency path: {value}')
    return Path(PurePosixPath(value))


def modules_from_text(text):
    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string(text)
    result, paths = [], set()
    for section in parser.sections():
        if not section.startswith('submodule "') or not section.endswith('"'):
            raise ValueError(f'Unexpected submodule section: {section}')
        name = section[len('submodule "'):-1]
        path, url = parser[section]['path'], parser[section]['url']
        safe_relative(path)
        if path.casefold() in paths:
            raise ValueError('Duplicate dependency path')
        paths.add(path.casefold())
        if not re.fullmatch(r'https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/?', url):
            raise ValueError(f'Unsupported dependency URL: {url}')
        result.append(dict(name=name, path=path, url=url))
    return result


def index_links(repo):
    result = {}
    for raw in git(repo, 'ls-files', '--stage', '-z').split(b'\0'):
        if raw.startswith(b'160000 '):
            fields, name = raw.decode('utf-8').split('\t', 1)
            _, oid, stage = fields.split(' ')
            if stage != '0':
                raise ValueError('Unmerged dependency index')
            safe_relative(name)
            result[name] = oid
    return result


def locked_modules(repo):
    repo = Path(repo)
    lock = repo / 'tools/sdk-source.lock.json'
    module_file = repo / '.gitmodules'
    if not lock.exists() and not module_file.exists():
        return []
    reject_link(lock); reject_link(module_file)
    items = json.loads(lock.read_text(encoding='utf-8'))['submodules']
    declared = {item['path']: item for item in modules_from_text(module_file.read_text(encoding='utf-8'))}
    seen = set()
    for item in items:
        safe_relative(item['path'])
        if (not item['path'].startswith('third_party/rexglue/thirdparty/') or
                item['path'] in seen or not re.fullmatch('[0-9a-f]{40}', item['commit']) or
                declared.get(item['path']) != {key: item[key] for key in ('name', 'path', 'url')}):
            raise ValueError('SDK dependency lock differs from .gitmodules')
        seen.add(item['path'])
    if seen != set(declared):
        raise ValueError('Unlocked SDK dependency')
    return items


def restore(repo, cache=None, top=True):
    repo = Path(repo).resolve()
    module_file = repo / '.gitmodules'
    if not module_file.exists():
        return 0
    reject_link(module_file)
    modules = (locked_modules(repo) if top else
               modules_from_text(module_file.read_text(encoding='utf-8')))
    links = index_links(repo)
    if set(links) != {item['path'] for item in modules}:
        raise ValueError('Dependency index and .gitmodules differ')
    restored = 0
    for item in modules:
        relative = safe_relative(item['path'])
        target = repo / relative
        reject_link(target)
        expected = links[item['path']]
        if top and expected != item['commit']:
            raise ValueError('Dependency index differs from the SDK lock')
        seed = Path(cache) / relative if cache else None
        if (target / '.git').exists():
            head = git(target, 'rev-parse', 'HEAD').decode('ascii').strip()
            if head != expected or git(target, 'status', '--porcelain', '--untracked-files=normal').strip():
                raise ValueError(f'Existing dependency differs or is dirty; preserve it: {target}')
        else:
            if target.exists() and any(target.iterdir()):
                raise ValueError(f'Existing nonempty dependency is preserved: {target}')
            source = item['url']
            if seed and (seed / '.git').exists() and git(seed, 'rev-parse', 'HEAD').decode('ascii').strip() == expected:
                source = str(seed.resolve())
            print(f'Restoring {item["path"]} at {expected[:12]}', flush=True)
            # A cached source is an optimization only. No alternates, hardlinked
            # object files or old-project remote URLs remain in the new checkout.
            clone = ['git', 'clone', '--no-local', '--no-hardlinks', '--no-checkout']
            if source != item['url']:
                clone += ['--depth', '1']
            subprocess.run(clone + ['--', source, str(target)], check=True)
            subprocess.run(['git', '-C', str(target), 'checkout', '--detach', expected], check=True)
            git(target, 'remote', 'set-url', 'origin', item['url'])
            git(repo, 'submodule', 'absorbgitdirs', '--', item['path'])
        git(repo, 'config', f'submodule.{item["name"]}.url', item['url'])
        git(repo, 'config', f'submodule.{item["name"]}.active', 'true')
        restored += 1 + restore(target, seed if seed and seed.exists() else None, top=False)
        alternate = git(target, 'rev-parse', '--path-format=absolute', '--git-path',
                        'objects/info/alternates').decode('utf-8').strip()
        if Path(alternate).exists():
            raise ValueError(f'Dependency has external Git object alternates: {target}')
    return restored


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['restore'])
    parser.add_argument('--cache-from', type=Path)
    args = parser.parse_args()
    print(json.dumps(dict(verifiedDependencies=restore(ROOT, args.cache_from)), indent=2))
