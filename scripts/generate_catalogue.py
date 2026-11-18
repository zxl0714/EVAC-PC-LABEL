"""Validate authored game metadata and compile it into the native catalogue."""
import argparse
import json
from pathlib import Path, PurePosixPath
import re


PS4_SFO = 'sce_sys/param.sfo'
PS4_EXPORT_METADATA = (PS4_SFO, 'sce_sys/playgo-chunk.sha', 'sce_sys/about/right.sprx')


def ps4_required_path(path):
    # The host uses the application SFO and the original libc. The other
    # system libraries and packaging files are not required to start a game.
    return path == PS4_SFO or not (
        path.startswith('sce_sys/') or path in ('sce_discmap.plt', 'sce_discmap_patch.plt') or
        (path.startswith('sce_module/libSce') and path.endswith('.prx')))


def ps4_required_files(files):
    return [file for file in files if ps4_required_path(file['path'])]


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


def data_files(items):
    if not isinstance(items, list) or not items or len(items) > 100000:
        raise ValueError('Invalid variant file list')
    seen = set()
    for item in items:
        name = relative(item['path'])
        if (name.casefold() in seen or type(item['bytes']) is not int or not 0 <= item['bytes'] <= 2**63-1
                or not re.fullmatch('[0-9a-f]{64}', item['sha256']) or type(item['program']) is not bool):
            raise ValueError('Invalid variant file identity')
        fingerprint = item.get('programFingerprint')
        if 'programFingerprint' in item and (not item['program'] or not isinstance(fingerprint, str) or not re.fullmatch('[0-9a-f]{64}', fingerprint)):
            raise ValueError('Invalid PS4 program fingerprint')
        seen.add(name.casefold())
    return seen


def data_variants(manifest):
    alternatives = manifest.get('alternatives', [])
    if not isinstance(alternatives, list) or len(alternatives) > 8:
        raise ValueError('Invalid alternate data variants')
    if 'variantId' not in manifest:
        if alternatives:
            raise ValueError('Alternate data requires a primary variant')
        return []
    variants = [dict(id=manifest['variantId'], version=manifest['version'], layout=manifest['layout'],
                     entry=manifest['entry'], files=manifest['files'], forbiddenFiles=manifest.get('forbiddenFiles', [])),
                *alternatives]

    def runtime_identity(value):
        inputs = {f['path']: f.get('programFingerprint') if f['program'] else (f['bytes'], f['sha256'])
                  for f in ps4_required_files(value['files']) if f['path'] != PS4_SFO}
        forbidden = {p for p in value.get('forbiddenFiles', []) if ps4_required_path(p)}
        return inputs, forbidden

    def export_identity(value):
        hashes = {f['path']: f['sha256'] for f in value['files'] if not f['program']}
        return tuple(hashes.get(path) for path in PS4_EXPORT_METADATA)

    ids, identities, fingerprints = set(), {}, {}
    for variant in variants:
        if (not isinstance(variant, dict) or not isinstance(variant.get('id'), str) or not re.fullmatch('[a-z][a-z0-9-]{0,31}', variant['id'])
                or variant['id'] in ids or not isinstance(variant.get('version'), str) or not re.fullmatch('[0-9]{2}\\.[0-9]{2}', variant['version'])
                or variant['layout'] not in ('base', 'merged-patch') or variant['entry'] != variants[0]['entry']
                or variant['entry'] not in ('default.xex', 'eboot.bin')):
            raise ValueError('Invalid or duplicate data variant identity')
        ids.add(variant['id'])
        seen = data_files(variant['files'])
        entries = [item for item in variant['files'] if item['path'] == variant['entry'] and item['program']]
        if len(entries) != 1:
            raise ValueError('Data variants require one root program')
        if variant['entry'] == 'eboot.bin':
            title_id = variant.get('titleId', manifest.get('titleId', ''))
            content_id = variant.get('contentId', manifest.get('contentId', ''))
            if not re.fullmatch(r'CUSA[0-9]{5}', title_id) or (content_id and
                    not re.fullmatch(r'[A-Z]{2}[0-9]{4}-' + title_id + r'_00-[A-Z0-9]{16}', content_id)):
                raise ValueError('Invalid PS4 application identity')
            variant['titleId'], variant['contentId'] = title_id, content_id
        forbidden = variant.get('forbiddenFiles', [])
        if not isinstance(forbidden, list) or len(forbidden) > 128:
            raise ValueError('Invalid variant forbidden files')
        names = set()
        for name in forbidden:
            relative(name)
            if name.casefold() in seen or name.casefold() in names:
                raise ValueError('Conflicting variant forbidden file')
            names.add(name.casefold())
        variant['forbiddenFiles'] = forbidden
        fingerprint = entries[0].get('programFingerprint')
        if variant['entry'] != 'eboot.bin' and any('programFingerprint' in item for item in variant['files']):
            raise ValueError('Program fingerprints require PS4 input')
        previous_fingerprint = identities.get(entries[0]['sha256'])
        if entries[0]['sha256'] in identities and (not fingerprint or fingerprint != previous_fingerprint):
            raise ValueError('Shared root hashes require identical program fingerprints')
        identities[entries[0]['sha256']] = fingerprint
        if fingerprint:
            if fingerprint in fingerprints:
                runtime = runtime_identity(variant)
                export = export_identity(variant)
                if (None in runtime[0].values() or not export[0] or
                        any(any(variant[key] != previous[key] for key in ('version', 'layout', 'entry', 'titleId'))
                            or runtime != runtime_identity(previous) or not export_identity(previous)[0]
                            or export == export_identity(previous) for previous in fingerprints[fingerprint])):
                    raise ValueError('Equivalent PS4 exports require the same runtime inputs and distinct source metadata')
            fingerprints.setdefault(fingerprint, []).append(variant)
    return variants


def xex_title_update(game, manifest):
    update = manifest.get('titleUpdate')
    if update is None:
        return None
    if (game['backend'] != 'xbox360' or game['variants'] or game['forbiddenFiles'] or
            not re.fullmatch(r'[a-z][a-z0-9-]{0,31}', update['id']) or
            not re.fullmatch(r'[0-9]+\.[0-9]{2}', update['version']) or
            not re.fullmatch(r'[0-9A-F]{8}', update['titleId']) or
            update['executable'] == game['executable'] or update['executable'] not in game['runtimeFiles']):
        raise ValueError('Invalid Xbox 360 title update identity')
    container = update['container']
    patches = update['patches']
    names = data_files([container, *patches])
    programs = {item['path'] + 'p' for item in game['files'] if item['program']}
    if ({item['path'] for item in patches} != programs or 'default.xexp' not in programs or
            any('/' in item['path'] or not item['program'] or item['bytes'] == 0
                for item in [container, *patches]) or
            names.intersection(item['path'].casefold() for item in game['files'])):
        raise ValueError('Title update payloads must match the base programs')
    end = 0
    for patch in sorted(patches, key=lambda item: item['offset']):
        offset = patch['offset']
        padded = patch.get('padded')
        if padded and (type(padded['bytes']) is not int or padded['bytes'] <= patch['bytes'] or
                       not re.fullmatch('[0-9a-f]{64}', padded['sha256'])):
            raise ValueError('Invalid padded title update identity')
        size = padded['bytes'] if padded else patch['bytes']
        if type(offset) is not int or offset < end or offset + size > container['bytes']:
            raise ValueError('Invalid title update payload range')
        end = offset + size
    return update


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
        if game['backend'] not in ('xbox360', 'ps4', 'ps2', 'undetermined'):
            raise ValueError('Invalid backend')
        if not isinstance(game['name'], str) or not game['name'] or any(ord(c) < 32 for c in game['name']):
            raise ValueError('Invalid display name')
        if 'nameJapanese' in game and (not isinstance(game['nameJapanese'], str) or not game['nameJapanese']
                or len(game['nameJapanese']) > 128 or any(ord(c) < 32 for c in game['nameJapanese'])):
            raise ValueError('Invalid Japanese display name')
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
        game['forbiddenFiles'] = []
        game['variants'] = []
        game['titleUpdate'] = None
        if game['dataManifest']:
            source = path.parent / relative(game['dataManifest'])
            if not source.resolve().is_relative_to(path.parent):
                raise ValueError('Linked data manifest escapes games directory')
            manifest = read_json(source)
            if manifest['format'] != 1 or manifest['game'] != game['id'] or not manifest['files']:
                raise ValueError('Invalid data manifest identity')
            seen = data_files(manifest['files'])
            if game['backend'] != 'ps4' and any('programFingerprint' in item for item in manifest['files']):
                raise ValueError('Program fingerprints require PS4 input')
            game['files'] = manifest['files']
            if game['backend'] == 'ps2':
                programs = [item for item in game['files'] if item['program']]
                discs = [item for item in game['files'] if not item['program']]
                if (manifest.get('layout') != 'iso' or len(programs) != 1 or len(discs) != 1
                        or discs[0]['path'] != 'disc.iso' or '/' in programs[0]['path']
                        or programs[0]['bytes'] == 0 or discs[0]['bytes'] < 18 * 2048
                        or manifest.get('optionalContent') or manifest.get('alternatives') or 'variantId' in manifest):
                    raise ValueError('PS2 ISO manifest needs one boot entry and disc identity')
            forbidden = manifest.get('forbiddenFiles', [])
            if not isinstance(forbidden, list) or len(forbidden) > 128:
                raise ValueError('Invalid forbidden file list')
            forbidden_names = set()
            for name in forbidden:
                relative(name)
                if name.casefold() in seen or name.casefold() in forbidden_names:
                    raise ValueError('Conflicting or duplicate forbidden file')
                forbidden_names.add(name.casefold())
            game['forbiddenFiles'] = forbidden
            game['variants'] = data_variants(manifest)
            game['titleUpdate'] = xex_title_update(game, manifest)
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
                native_name = package.get('nativeName', package['name'])
                if (not isinstance(native_name, str) or not native_name
                        or any(ord(c) < 32 for c in native_name)
                        or len(native_name.encode('utf-16-le')) > 254):
                    raise ValueError('Invalid optional content native name')
                identifier = package['id'].replace('-', '_')
                if identifier in content_identifiers:
                    raise ValueError('Optional content identifiers collide after compilation')
                content_identifiers.add(identifier)
                directory = f"0000000000000000/{package['titleId']}/00000002/{package['packageName']}"
                if (package['directory'] != directory or directory in content_paths or not package['files']):
                    raise ValueError('Invalid or duplicate optional content directory')
                content_paths.add(directory)
                names = set()
                for item in package['files']:
                    name = relative(item['path'])
                    if (name.casefold() in names or not name.startswith(directory + '/')
                            or type(item['bytes']) is not int or not 0 <= item['bytes'] <= 2**63-1
                            or not re.fullmatch('[0-9a-f]{64}', item['sha256']) or item['program'] is not False):
                        raise ValueError('Invalid optional content file')
                    names.add(name.casefold())
                if not names:
                    raise ValueError('Optional content requires payload files')
                game['content'].append(package)
    return games


def generate(games):
    quoted = lambda value: json.dumps(value, ensure_ascii=False)
    def file_row(file):
        fields = [quoted(file['path']), str(file['bytes'])+'ull', quoted(file['sha256']), str(file['program']).lower()]
        if 'programFingerprint' in file:
            fields.append(quoted(file['programFingerprint']))
        return '{' + ','.join(fields) + '},'
    lines = ['// Generated from authored metadata, never from game program bodies.',
             '#pragma once', '#include <array>', '#include "common/catalogue.h"', 'namespace cave::detail {']
    for game in games:
        key = game['id'].replace('-', '_')
        required = ps4_required_files if game['backend'] == 'ps4' else lambda files: files
        game_files = required(game['files'])
        lines.append(f'inline constexpr std::array<DataFile,{len(game_files)}> files_{key}{{{{')
        for file in game_files:
            lines.append(file_row(file))
        lines.append('}};')
        update = game['titleUpdate']
        if update:
            lines.append(f'inline constexpr std::array<XexTitlePatch,{len(update["patches"])}> update_patches_{key}{{{{')
            for patch in update['patches']:
                padded = patch.get('padded', {})
                lines.append('{' + ','.join([file_row(patch)[:-1], str(padded.get('bytes', 0)) + 'ull',
                                             quoted(padded.get('sha256', ''))]) + '},')
            lines.append('}};')
            fields = [quoted(update[name]) for name in ('id', 'version', 'executable')]
            fields += ['0x' + update['titleId'] + 'u', file_row(update['container'])[:-1], f'update_patches_{key}']
            lines.append(f'inline constexpr XexTitleUpdate update_{key}{{' + ','.join(fields) + '};')
        for index, variant in enumerate(game['variants']):
            variant_files = required(variant['files'])
            lines.append(f'inline constexpr std::array<DataFile,{len(variant_files)}> variant_files_{key}_{index}{{{{')
            for file in variant_files:
                lines.append(file_row(file))
            lines.append('}};')
            forbidden = variant['forbiddenFiles']
            if game['backend'] == 'ps4':
                forbidden = [path for path in forbidden if ps4_required_path(path)]
                metadata = [file for file in variant['files'] if file['path'] in PS4_EXPORT_METADATA]
                lines.append(f'inline constexpr std::array<DataFile,{len(metadata)}> variant_export_{key}_{index}{{{{')
                lines.extend(file_row(file) for file in metadata)
                lines.append('}};')
            lines.append(f'inline constexpr std::array<std::string_view,{len(forbidden)}> variant_forbidden_{key}_{index}{{{{')
            lines.extend(quoted(name) + ',' for name in forbidden)
            lines.append('}};')
        lines.append(f'inline constexpr std::array<DataVariant,{len(game["variants"])}> variants_{key}{{{{')
        for index, variant in enumerate(game['variants']):
            lines.append('{' + ','.join([quoted(variant[name]) for name in ('id', 'version', 'layout', 'entry')]
                         + [f'variant_files_{key}_{index}', f'variant_forbidden_{key}_{index}',
                            quoted(variant.get('titleId', '')),
                            f'variant_export_{key}_{index}' if game['backend'] == 'ps4' else '{}']) + '},')
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
                                        + ['0x' + package['titleId'] + 'u', f'content_files_{part}',
                                           quoted(package.get('nativeName', package['name']))]) + '},')
        lines.append('}};')
        lines.append(f'inline constexpr std::array<std::string_view,{len(game["runtimeFiles"])}> runtime_{key}{{{{')
        lines.extend(quoted(name) + ',' for name in game['runtimeFiles'])
        lines.append('}};')
        forbidden = game['forbiddenFiles']
        if game['backend'] == 'ps4':
            forbidden = [path for path in forbidden if ps4_required_path(path)]
        lines.append(f'inline constexpr std::array<std::string_view,{len(forbidden)}> forbidden_{key}{{{{')
        lines.extend(quoted(name) + ',' for name in forbidden)
        lines.append('}};')
    lines.append(f'inline const std::array<Game,{len(games)}> kCatalogue{{{{')
    for game in games:
        key = game['id'].replace('-', '_')
        fields = [quoted(game[k]) for k in ('id', 'name', 'backend', 'dataDirectory', 'executable')]
        fields += [str(game['integrated']).lower(), str(game['comingSoon']).lower(), f'files_{key}', f'runtime_{key}', f'content_{key}',
                   str(game.get('communitySupported', True)).lower(), f'forbidden_{key}', quoted(game.get('nameJapanese', '')), f'variants_{key}',
                   f'&update_{key}' if game['titleUpdate'] else 'nullptr']
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
