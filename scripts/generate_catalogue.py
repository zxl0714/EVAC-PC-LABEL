"""Validate authored game metadata and compile it into the native catalogue."""
import argparse
import json
from pathlib import Path, PurePosixPath
import re


def relative(value):
    if (not isinstance(value, str) or not value or '\\' in value or ':' in value
            or any(ord(c) < 32 or c in '<>"|?*' for c in value)):
        raise ValueError('Invalid manifest path')
    path = PurePosixPath(value)
    if path.is_absolute() or any(p in ('', '.', '..') or p.endswith((' ', '.')) for p in value.split('/')):
        raise ValueError('Unsafe manifest path')
    return value


def read_json(path):
    def unique(items):
        out = {}
        for key, value in items:
            if key in out:
                raise ValueError('Duplicate JSON key')
            out[key] = value
        return out
    return json.loads(path.read_text(encoding='utf-8-sig'), object_pairs_hook=unique)


def load(path):
    path = Path(path).resolve()
    catalogue = read_json(path)
    if type(catalogue['format']) is not int or catalogue['format'] != 1 or type(catalogue['runtimeContract']) is not int or catalogue['runtimeContract'] != 1:
        raise ValueError('Unsupported catalogue contract')
    games, ids, identifiers = catalogue['games'], set(), set()
    if not 1 <= len(games) <= 64:
        raise ValueError('Invalid game count')
    for game in games:
        if not re.fullmatch('[a-z][a-z0-9_-]{0,31}', game['id']) or game['id'] in ids:
            raise ValueError('Invalid or duplicate game ID')
        ids.add(game['id'])
        identifier = game['id'].replace('-', '_')
        if identifier in identifiers:
            raise ValueError('Game identifiers collide after compilation')
        identifiers.add(identifier)
        if game['backend'] not in ('xbox360', 'ps4', 'undetermined'):
            raise ValueError('Invalid backend')
        if not isinstance(game['name'], str) or not game['name'] or any(ord(c) < 32 for c in game['name']):
            raise ValueError('Invalid display name')
        if type(game['integrated']) is not bool or type(game['comingSoon']) is not bool:
            raise ValueError('Invalid implementation state')
        if type(game.get('communitySupported', True)) is not bool:
            raise ValueError('Invalid community capability')
        if game['integrated'] and (game['comingSoon'] or not game['dataManifest'] or not game['runtimeFiles']):
            raise ValueError('Integrated games need data and runtime requirements')
        relative(game['dataDirectory'])
        relative(game['executable'])
        if game['executable'] not in game['runtimeFiles'] and game['runtimeFiles']:
            raise ValueError('Executable missing from runtime requirements')
        seen = set()
        for entry in game['runtimeFiles']:
            relative(entry)
            if entry.casefold() in seen:
                raise ValueError('Duplicate runtime path')
            seen.add(entry.casefold())
        game['files'] = []
        game['content'] = []
        if game['dataManifest']:
            source = path.parent / relative(game['dataManifest'])
            if not source.resolve().is_relative_to(path.parent):
                raise ValueError('Linked data manifest escapes games directory')
            manifest = read_json(source)
            if manifest['format'] != 1 or manifest['game'] != game['id'] or not manifest['files']:
                raise ValueError('Invalid data manifest identity')
            seen = set()
            for item in manifest['files']:
                name = relative(item['path'])
                if name.casefold() in seen or type(item['bytes']) is not int or not 0 <= item['bytes'] <= 2**63-1:
                    raise ValueError('Duplicate data path or invalid size')
                seen.add(name.casefold())
                if not re.fullmatch('[0-9a-f]{64}', item['sha256']) or type(item['program']) is not bool:
                    raise ValueError('Invalid hash or program flag')
            game['files'] = manifest['files']
            content_ids, content_paths, content_identifiers = set(), set(), set()
            if not isinstance(manifest.get('optionalContent', []), list) or len(manifest.get('optionalContent', [])) > 32:
                raise ValueError('Invalid optional content count')
            for package in manifest.get('optionalContent', []):
                if (not re.fullmatch('[a-z][a-z0-9_-]{0,31}', package['id']) or package['id'] in content_ids
                        or not isinstance(package['name'], str) or not package['name']
                        or any(ord(c) < 32 for c in package['name'])
                        or not re.fullmatch('[0-9A-F]{8}', package['titleId'])
                        or not re.fullmatch('[0-9A-F]{40,64}', package['packageName'])):
                    raise ValueError('Invalid optional content identity')
                content_ids.add(package['id'])
                identifier = package['id'].replace('-', '_')
                if identifier in content_identifiers:
                    raise ValueError('Optional content identifiers collide after compilation')
                content_identifiers.add(identifier)
                directory = f"0000000000000000/{package['titleId']}/00000002/{package['packageName']}"
                header = f"0000000000000000/{package['titleId']}/Headers/00000002/{package['packageName']}.header"
                if (package['directory'] != directory or directory in content_paths or not package['files']):
                    raise ValueError('Invalid or duplicate optional content directory')
                content_paths.add(directory)
                names = set()
                for item in package['files']:
                    name = relative(item['path'])
                    if (name.casefold() in names or not (name.startswith(directory + '/') or name == header)
                            or type(item['bytes']) is not int or not 0 <= item['bytes'] <= 2**63-1
                            or not re.fullmatch('[0-9a-f]{64}', item['sha256']) or item['program'] is not False):
                        raise ValueError('Invalid optional content file')
                    names.add(name.casefold())
                if header.casefold() not in names or len(names) < 2:
                    raise ValueError('Optional content requires its header and payload')
                game['content'].append(package)
    return games


def generate(games):
    quoted = lambda value: json.dumps(value, ensure_ascii=False)
    lines = ['// Generated from authored metadata, never from game program bodies.',
             '#pragma once', '#include <array>', '#include "common/catalogue.h"', 'namespace cave::detail {']
    for game in games:
        key = game['id'].replace('-', '_')
        lines.append(f'inline constexpr std::array<DataFile,{len(game["files"])}> files_{key}{{{{')
        for file in game['files']:
            lines.append('{' + ','.join([quoted(file['path']), str(file['bytes'])+'ull', quoted(file['sha256']), str(file['program']).lower()]) + '},')
        lines.append('}};')
        for package in game['content']:
            part = key + '_' + package['id'].replace('-', '_')
            lines.append(f'inline constexpr std::array<DataFile,{len(package["files"])}> content_files_{part}{{{{')
            for file in package['files']:
                lines.append('{' + ','.join([quoted(file['path']), str(file['bytes'])+'ull', quoted(file['sha256']), 'false']) + '},')
            lines.append('}};')
        lines.append(f'inline constexpr std::array<ContentPackage,{len(game["content"])}> content_{key}{{{{')
        for package in game['content']:
            part = key + '_' + package['id'].replace('-', '_')
            lines.append('{' + ','.join([quoted(package[k]) for k in ('id', 'name', 'directory', 'packageName')]
                                        + ['0x' + package['titleId'] + 'u', f'content_files_{part}']) + '},')
        lines.append('}};')
        lines.append(f'inline constexpr std::array<std::string_view,{len(game["runtimeFiles"])}> runtime_{key}{{{{')
        lines.extend(quoted(name) + ',' for name in game['runtimeFiles'])
        lines.append('}};')
    lines.append(f'inline const std::array<Game,{len(games)}> kCatalogue{{{{')
    for game in games:
        key = game['id'].replace('-', '_')
        fields = [quoted(game[k]) for k in ('id', 'name', 'backend', 'dataDirectory', 'executable')]
        fields += [str(game['integrated']).lower(), str(game['comingSoon']).lower(), f'files_{key}', f'runtime_{key}', f'content_{key}',
                   str(game.get('communitySupported', True)).lower()]
        lines.append('{' + ','.join(fields) + '},')
    return '\n'.join(lines + ['}};', '}  // namespace cave::detail', ''])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('catalogue', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = generate(load(args.catalogue))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding='utf-8') != result:
        args.output.write_text(result, encoding='utf-8', newline='\n')
