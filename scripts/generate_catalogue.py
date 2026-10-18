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
        lines.append(f'inline constexpr std::array<std::string_view,{len(game["runtimeFiles"])}> runtime_{key}{{{{')
        lines.extend(quoted(name) + ',' for name in game['runtimeFiles'])
        lines.append('}};')
    lines.append(f'inline const std::array<Game,{len(games)}> kCatalogue{{{{')
    for game in games:
        key = game['id'].replace('-', '_')
        fields = [quoted(game[k]) for k in ('id', 'name', 'backend', 'dataDirectory', 'executable')]
        fields += [str(game['integrated']).lower(), str(game['comingSoon']).lower(), f'files_{key}', f'runtime_{key}']
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
