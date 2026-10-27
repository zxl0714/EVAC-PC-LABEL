"""Build Windows packages; retain verification of historical release formats."""
import argparse
import configparser
import datetime as dt
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import uuid
import zipfile

from build_receipt import verify_receipt, verify_sdk
from generate_catalogue import load, relative
from launcher_receipt import verify as verify_launcher
from project import ROOT, SECRET, audit_source, digest, reject_link, write_json_new
from ps4_build_receipt import verify as verify_ps4
import ps4_extract_tools
from product import NAME, VERSION, LAUNCHER, LEGACY_LAUNCHER, REMOTE, REPOSITORY_URL, launcher, renamed, slug

LEGACY_GAMES = ('futari', 'espgaluda2', 'ketsui')
GAMES = (*LEGACY_GAMES, 'sdoj')
V03_GAMES = (*GAMES, 'daioujou')
V06_GAMES = (*V03_GAMES, 'esprade')
V07_GAMES = (*V06_GAMES, 'guwange')
PLACEMENT_GAMES = V07_GAMES
PLACEMENT_NAME = 'README.txt'
LEGACY_PLACEMENT_NAME = '放置说明.txt'  # Read-only verification of frozen releases.
DEFAULT_VERSION = VERSION
MANIFEST_ONLY_DATA = {'sdoj', 'daioujou', 'ketsui', 'esprade', 'guwange'}
META = {'release.json', 'SHA256SUMS.txt'}
UPDATE_BASES = {'0.3.1': '0.3', '0.3.2': '0.3', '0.4': '0.3'}
UPDATE_PRESERVED = ('config.ini', 'user-data/', 'game-data/', 'prepared/', 'cache/', 'logs/', 'source/')
PROGRAM_PRESERVED = tuple(name for name in UPDATE_PRESERVED if name != 'game-data/')


def batch_name(version=None):
    current = version or DEFAULT_VERSION
    return 'Launch.bat' if tuple(map(int, current.split('-', 1)[0].split('.')[:2])) >= (0, 6) else '启动游戏.bat'


def git(root, *args):
    return subprocess.check_output(['git', '--no-optional-locks', '-C', str(root), *args], stderr=subprocess.PIPE)


def version_name(version, flavor, package_format=1):
    if not re.fullmatch(r'[0-9]+\.[0-9]+(?:\.[0-9]+)?(?:-[a-z0-9.-]+)?', version) or flavor not in ('Internal', 'Public', 'Update'):
        raise ValueError('Expected a numeric version and Internal/Public/Update flavor')
    if package_format in (2, 3):
        if flavor not in ('Public', 'Internal') or not renamed(version):
            raise ValueError('Windows packages require v0.4 or later and Public/Internal flavor')
        return slug(version) + '-v' + version + '-win-x64' + ('-internal' if flavor == 'Internal' else '')
    if package_format != 1:
        raise ValueError('Unsupported package format')
    if flavor == 'Update' and version not in UPDATE_BASES:
        raise ValueError('No update baseline for this release version')
    return slug(version) + '-v' + version + '-' + flavor.lower()


def release_games(version):
    version_name(version, 'Public')
    major, minor = map(int, version.split('-', 1)[0].split('.')[:2])
    if (major, minor) == (0, 1):
        return LEGACY_GAMES
    if (major, minor) == (0, 2):
        return GAMES
    if (major, minor) in ((0, 3), (0, 4)):
        return V03_GAMES
    if (major, minor) == (0, 6):
        return V06_GAMES
    if (major, minor) == (0, 7):
        return V07_GAMES
    raise ValueError('No game-set profile for this release version')


def catalogue(selection=GAMES):
    games = {game['id']: game for game in load(ROOT / 'games/catalogue.json')}
    if any(not games[key]['integrated'] or games[key]['comingSoon'] or not games[key]['runtimeFiles']
           or not games[key].get('communitySupported', True) for key in selection):
        raise ValueError('All selected release games must be integrated with community support: ' + ', '.join(selection))
    return games


def selected_data_files(game, variant=None):
    if not game.get('variants'):
        if variant:
            raise ValueError('Unexpected data variant')
        return game['files']
    matches = [item['files'] for item in game['variants'] if item['id'] == variant]
    if len(matches) != 1:
        raise ValueError('A supported data variant must be explicitly identified')
    return matches[0]


def config_name(version):
    release_games(version)
    return 'user-data/config.ini' if version in UPDATE_BASES or renamed(version) else 'config.ini'


def placement_sources(version, name=PLACEMENT_NAME):
    if name not in (PLACEMENT_NAME, LEGACY_PLACEMENT_NAME):
        raise ValueError('Unknown placement guide name')
    if not version or not renamed(version):
        return {}
    games = release_games(version)
    # Old v0.4/v0.6 packages included the six established placement folders.
    # Adding a title must not invalidate their frozen inventories.
    placement = V07_GAMES if 'guwange' in games else V06_GAMES
    return {'game-data/' + key + '/' + name: ROOT / 'config/game-data' / (key + '.txt')
            for key in placement}


def extraction_sources(version):
    parts = tuple(map(int, (version or '0.0').split('-', 1)[0].split('.')))
    result = {'tools/Extract-Ps4Pkg.ps1': ROOT / 'scripts/Extract-Ps4Pkg.ps1'} if parts >= (0, 6, 1) else {}
    if parts >= (0, 7):
        result['tools/Extract-Xbla.ps1'] = ROOT / 'scripts/Extract-Xbla.ps1'
    if parts >= (0, 7, 3):
        result['tools/Extract-Dlc.ps1'] = ROOT / 'scripts/Extract-Dlc.ps1'
    return result


def has_ps4_extractor(version):
    return tuple(map(int, (version or '0.0').split('-', 1)[0].split('.'))) >= (0, 7, 6)


def extractor_sources(version):
    return ps4_extract_tools.player_sources(ROOT) if has_ps4_extractor(version) else {}


def extractor_role(name):
    if name.endswith(('.exe', '.dll')):
        return 'runtime'
    return 'license' if Path(name).name in ('LICENSE.txt', 'GPL-3.0.txt', 'SOURCE.txt') else 'documentation'


def runtime_names(games, symbols=False, selection=GAMES, version=None, flavor=None, package_format=1):
    names = {launcher(version), 'runtime/launcher/cave_inspect.exe'}
    if has_ps4_extractor(version):
        names.add('tools/Extract-PS4.exe')
    # Historical packages refreshed old shortcuts. Format 3 ships only the
    # current launcher name, including when matching symbols are selected.
    if renamed(version) and (flavor == 'Update' or package_format == 2):
        names.add(LEGACY_LAUNCHER)
    for key in selection:
        names.update(games[key]['runtimeFiles'])
    if symbols:
        names.update(str(Path(name).with_suffix('.pdb')).replace('\\', '/') for name in list(names)
                     if name.endswith(('.exe', '.dll')))
    return names


def safe_name(name):
    return relative(name)


def tree_files(root):
    reject_link(root)
    if not root.is_dir():
        raise ValueError('Missing source directory: ' + root.name)
    result = []
    for directory, folders, files in os.walk(root, followlinks=False):
        for name in [*folders, *files]:
            # The root's entire ancestor chain was checked above. Each child
            # is checked before os.walk can descend into it, so rechecking all
            # those same ancestors for every file adds no traversal protection.
            child = Path(directory) / name
            if child.is_symlink() or child.is_junction():
                raise ValueError(f'Linked source/output: {child.name}')
        result.extend(Path(directory) / name for name in files)
    return sorted(result)


def copy_checked(source, target, role, entries, expected=None):
    reject_link(source); reject_link(target)
    before = digest(source)
    if expected and (before != expected['sha256'] or source.stat().st_size != expected['bytes']):
        raise ValueError('Source differs from its certificate: ' + source.name)
    target.parent.mkdir(parents=True, exist_ok=True)
    with source.open('rb') as original, target.open('xb') as destination:
        shutil.copyfileobj(original, destination, 1024 * 1024)
    if digest(target) != before or digest(source) != before:
        raise ValueError('Copy verification failed: ' + source.name)
    entries.append(dict(path=target, bytes=target.stat().st_size, sha256=before, role=role))


def text_new(target, value, role, entries):
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(value)
    entries.append(dict(path=target, bytes=target.stat().st_size, sha256=digest(target), role=role))


def certified_runtime(selection=GAMES):
    if git(ROOT, 'status', '--porcelain', '--untracked-files=normal'):
        raise ValueError('Commit source changes before release packaging')
    verify_sdk()
    games = catalogue(selection)
    xbox = tuple(sorted(key for key in selection if games[key]['backend'] == 'xbox360'))
    records = dict(xbox360=verify_receipt('host', xbox), ps4=verify_ps4(), launcher=verify_launcher())
    commit = git(ROOT, 'rev-parse', 'HEAD').decode().strip()
    if any(record['sourceCommit'] != commit for record in records.values()):
        raise ValueError('Rebuild all player components at the release commit')
    audit_source()
    xbox_names = {Path(name).name for key in xbox for name in games[key]['runtimeFiles'] if name.endswith(('.exe', '.dll'))}
    xbox_names |= {str(Path(name).with_suffix('.pdb')) for name in list(xbox_names)}
    selected = dict(xbox360=xbox_names,
        ps4={'KetsuiNativeProbe.exe', 'KetsuiNativeProbe.pdb', 'KetsuiGpuRuntime.dll', 'KetsuiGpuRuntime.pdb'},
        launcher={LAUNCHER, NAME + '.pdb', 'cave_inspect.exe', 'cave_inspect.pdb', 'Extract-PS4.exe', 'Extract-PS4.pdb'})
    certified = {}
    for backend, record in records.items():
        for item in record['outputs']:
            if backend == 'xbox360':
                source = ROOT / 'out/xbox360/bin' / item['path']
            else:
                source = ROOT / item['path']
            name = source.name
            if name not in selected[backend]:
                continue  # Do not confuse dev/test tools with the player runtime.
            if name in certified and certified[name]['sha256'] != item['sha256']:
                raise ValueError('Ambiguous certified runtime name: ' + name)
            certified[name] = dict(item, source=source)
    return commit, records, certified


def inspect_game(inspector, key, data=None, dlc=None):
    command = [str(inspector), '--root', str(ROOT), '--game', key, '--full']
    if data:
        command += ['--data-dir', str(data)]
    if dlc:
        command += ['--dlc-dir', str(dlc)]
    report = json.loads(subprocess.check_output(command))['games'][0]
    if report['dataState'] != 'verified' or report['problems']:
        raise ValueError('Complete supported data required for Internal: ' + key)
    return report


def content_inputs(dlc, packages, report):
    """Read the inspector's resolved inputs; keep canonical package outputs."""
    checks = {item['id']: item for item in report.get('optionalContent', [])}
    result = {}
    for package in packages:
        canonical = package.get('directory', '')
        check = checks.get(package['id'], {})
        base = Path(check.get('directory') or dlc / canonical).resolve(strict=True)
        if not base.is_relative_to(dlc.resolve(strict=True)):
            raise ValueError('Resolved DLC directory escapes source')
        for file in package['files']:
            relative = Path(file['path']).relative_to(canonical) if canonical else Path(file['path'])
            source = base / relative
            reject_link(source)
            if not source.is_file() or source.stat().st_size != file['bytes'] or digest(source) != file['sha256']:
                raise ValueError('Internal requires complete selected Futari DLC: ' + package['id'])
            result[file['path']] = source
    return result


def copy_data(source, target, entries, required=(), exclude_top=(), manifest_only=False):
    source = source.resolve(strict=True)
    expected = {item['path']: item for item in required}
    copied = set()
    files = [source / safe_name(name) for name in sorted(expected)] if manifest_only else tree_files(source)
    if manifest_only and not expected:
        raise ValueError('Manifest-only data copy requires an explicit resource list')
    for file in files:
        name = file.relative_to(source).as_posix(); safe_name(name)
        parts = Path(name).parts
        if parts[0].casefold() in exclude_top:
            continue
        if any(part.casefold() in {'.git', '.ssh', 'user-data', 'logs', 'credentials'} for part in parts) or file.suffix.lower() in {'.pem', '.key', '.dmp'}:
            raise ValueError('Unexpected private/diagnostic material inside game data: ' + name)
        copy_checked(file, target / name, 'game-data', entries, expected.get(name))
        copied.add(name)
    if set(expected) - copied:
        raise ValueError('Required data disappeared while copying')


def source_clone(destination, commit):
    subprocess.run(['git', '-c', 'core.longpaths=true', 'clone', '--no-local', '--no-hardlinks', '--no-checkout',
                    str(ROOT), str(destination)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    git(destination, 'config', 'core.longpaths', 'true')
    subprocess.run(['git', '-C', str(destination), 'checkout', '-B', 'main', commit], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    git(destination, 'remote', 'set-url', 'origin', REMOTE)
    if git(destination, 'rev-parse', 'HEAD').decode().strip() != commit or (destination / '.git/objects/info/alternates').exists():
        raise ValueError('Source clone is not independent or belongs to another commit')
    git(destination, 'fsck', '--full', '--no-reflogs')
    audit_source(destination)


def license_files():
    paths = {ROOT / 'third_party/rexglue/LICENSE', ROOT / 'third_party/ps4/THIRD_PARTY_NOTICES.txt'}
    paths.add(ROOT / 'third_party/rexglue/thirdparty/inja/third_party/include/nlohmann/LICENSE.MIT')
    paths.update((ROOT / 'third_party/ps4/LICENSES').glob('*.txt'))
    for directory in (ROOT / 'third_party/rexglue/thirdparty').iterdir():
        if directory.is_dir():
            for pattern in ('LICENSE*', 'COPYING*', 'NOTICE*'):
                paths.update(path for path in directory.glob(pattern) if path.is_file())
    return sorted(paths)


def redist_directory():
    locator = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    installation = subprocess.check_output([str(locator), '-latest', '-products', '*', '-version', '[17.0,18.0)',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath']).decode().strip()
    candidates = [path for path in (Path(installation) / 'VC/Redist/MSVC').glob('*/x64/Microsoft.VC143.CRT')
                  if re.fullmatch(r'\d+(?:\.\d+)+', path.parents[1].name)]
    if not candidates:
        raise ValueError('VS 2022 x64 app-local CRT redistributables are not installed')
    return max(candidates, key=lambda path: tuple(int(value) for value in path.parents[1].name.split('.')))


def validate_public_name(name, games, selection=GAMES, version=None, placement_name=PLACEMENT_NAME):
    allowed = runtime_names(games, selection=selection, version=version) | {'config.ini', 'user-data/config.ini', 'README.txt', batch_name(version), 'SOURCE.txt'}
    allowed.update(placement_sources(version, placement_name))
    allowed.update(extraction_sources(version))
    allowed.update(extractor_sources(version))
    if name in allowed or name.startswith('licenses/'):
        return
    # App-local CRT copies only, not arbitrary DLLs supplied by an input tree.
    parent, base = Path(name).parent.as_posix(), Path(name).name.lower()
    if parent in ('.', 'runtime/xbox360', 'runtime/ps4', 'runtime/launcher') and re.fullmatch(
            r'(?:msvcp140(?:_[12]|_atomic_wait|_codecvt_ids)?|vcruntime140(?:_1|_threads)?|concrt140|vccorlib140)\.dll', base):
        return
    raise ValueError('File is not in the Public allowlist: ' + name)


def validate_update_name(name, games, selection=V03_GAMES, version=None, placement_name=PLACEMENT_NAME):
    normalized = safe_name(name).casefold()
    if any(normalized == item.rstrip('/') or normalized.startswith(item)
           for item in UPDATE_PRESERVED):
        raise ValueError('Update must preserve existing player files: ' + name)
    if renamed(version) and normalized == LEGACY_LAUNCHER.casefold():
        return
    validate_public_name(name, games, selection, version, placement_name)


def selected_items(requested, allowed, label):
    if isinstance(requested, (str, bytes)):
        raise ValueError('Invalid ' + label + ' selection')
    requested = list(requested or ())
    if any(not isinstance(item, str) for item in requested):
        raise ValueError('Invalid ' + label + ' selection')
    if requested == ['all']:
        return list(allowed)
    if len(set(requested)) != len(requested) or any(item not in allowed for item in requested):
        raise ValueError('Invalid or duplicate ' + label + ' selection')
    return [item for item in allowed if item in requested]


def windows_policy(record, games, selection):
    """Validate the flat Windows contract independently of its file inventory."""
    version_name(record['version'], record['flavor'], record['format'])
    data_games, content = record.get('dataGames'), record.get('futariContent')
    if not isinstance(data_games, list) or not isinstance(content, list):
        raise ValueError('Missing explicit Internal content selection')
    packages = {item['id']: item for item in games['futari']['content']}
    if (data_games != selected_items(data_games, selection, 'game data')
            or content != selected_items(content, packages, 'Futari DLC')
            or type(record.get('symbolsIncluded')) is not bool
            or (content and 'futari' not in data_games)
            or (record['flavor'] == 'Public' and (data_games or content or record['symbolsIncluded']))):
        raise ValueError('Invalid package content selection')
    commit = record.get('sourceCommit', '')
    if (not isinstance(commit, str) or not re.fullmatch('[0-9a-f]{40}', commit)
            or record.get('sourceUrl') != REPOSITORY_URL + '/tree/' + commit
            or record.get('sourceIncluded') is not False
            or record.get('platform') != 'win-x64'
            or record.get('archiveLayout') != 'flat'
            or record.get('preservedPaths') != list(PROGRAM_PRESERVED)):
        raise ValueError('Invalid Windows package/source identity')
    variants = record.get('dataVariants', {})
    expected_variants = {key for key in data_games if games[key].get('variants')}
    if not isinstance(variants, dict) or set(variants) != expected_variants:
        raise ValueError('Data variants differ from the selected games')
    required = {}
    for key in data_games:
        for item in selected_data_files(games[key], variants.get(key)):
            required['game-data/' + key + '/' + item['path']] = item
    for key in content:
        for item in packages[key]['files']:
            required['game-data/futari/dlc/' + item['path']] = item
    return required


def validate_windows_item(item, record, games, selection, required, runtime):
    name = item['path']
    if name in required:
        identity = required[name]
        if item['role'] != 'game-data' or any(item[key] != identity[key] for key in ('bytes', 'sha256')):
            raise ValueError('Selected game data differs from its manifest: ' + name)
        return
    normalized = name.casefold()
    if any(normalized == path.rstrip('/') or normalized.startswith(path) for path in PROGRAM_PRESERVED):
        raise ValueError('Package would overwrite player files: ' + name)
    if name in runtime:
        if item['role'] != 'runtime':
            raise ValueError('Invalid runtime role: ' + name)
        return
    if name.startswith('game-data/'):
        if name not in placement_sources(record['version'], record.get('placementGuideName', LEGACY_PLACEMENT_NAME)) or item['role'] != 'documentation':
            raise ValueError('Unselected game data in package: ' + name)
        return
    validate_public_name(name, games, selection, record['version'], record.get('placementGuideName', LEGACY_PLACEMENT_NAME))
    if name in extractor_sources(record['version']):
        if item['role'] != extractor_role(name):
            raise ValueError('Invalid PS4 extraction component role: ' + name)
        expected_sha = ps4_extract_tools.lock(ROOT)['files'].get(Path(name).name)
        if expected_sha and item.get('sha256') != expected_sha:
            raise ValueError('PS4 extraction dependency differs from its pin: ' + name)
        return
    if name in extraction_sources(record['version']):
        if item['role'] != 'documentation':
            raise ValueError('Invalid extraction-helper role: ' + name)
        return
    if item['role'] not in ('documentation', 'license', 'redist') or Path(name).suffix.lower() in (
            '.pdb', '.cpp', '.h', '.xex', '.ktprobe', '.ktmodule', '.ktfix', '.py', '.ps1'):
        raise ValueError('Source, symbols or unexpected payload: ' + name)


def verify_tree(directory, games=None):
    release = json.loads((directory / 'release.json').read_text(encoding='utf-8'))
    selection = release_games(release.get('version', ''))
    games = games or catalogue(selection)
    package_format = release.get('format')
    guide_name = release.get('placementGuideName', LEGACY_PLACEMENT_NAME)
    guides = placement_sources(release['version'], guide_name) if release['flavor'] != 'Update' else {}
    if type(package_format) is not int or package_format not in (1, 2, 3) or release.get('flavor') not in ('Internal', 'Public', 'Update') or release.get('games') != list(selection):
        raise ValueError('Invalid release identity')
    required_data = windows_policy(release, games, selection) if package_format in (2, 3) else {}
    symbols = release['symbolsIncluded'] if package_format in (2, 3) else release['flavor'] == 'Internal'
    runtime = runtime_names(games, symbols, selection, release['version'], release['flavor'], package_format)
    if release['flavor'] == 'Update' and (release['version'] not in UPDATE_BASES
            or release.get('baseVersion') != UPDATE_BASES.get(release['version'])
            or release.get('preservedPaths') != list(UPDATE_PRESERVED)):
        raise ValueError('Invalid update baseline or preservation contract')
    expected = {}
    for item in release['files']:
        name = safe_name(item['path'])
        if name in expected or name in META:
            raise ValueError('Duplicate or recursive package inventory entry')
        if package_format in (2, 3):
            validate_windows_item(item, release, games, selection, required_data, runtime)
        elif release['flavor'] in ('Public', 'Update'):
            validator = validate_update_name if release['flavor'] == 'Update' else validate_public_name
            validator(name, games, selection, release['version'], guide_name)
            allowed_roles = ('runtime', 'documentation', 'license', 'redist') if release['flavor'] == 'Update' else (
                'runtime', 'config', 'documentation', 'license', 'redist')
            if item['role'] not in allowed_roles:
                raise ValueError('Forbidden Public package role')
            if Path(name).suffix.lower() in ('.pdb', '.cpp', '.h', '.xex', '.ktprobe', '.ktmodule', '.ktfix', '.py', '.ps1'):
                raise ValueError('Source, symbols or game body in Public package')
        file = directory / name; reject_link(file)
        if not file.is_file() or file.stat().st_size != item['bytes'] or digest(file) != item['sha256']:
            raise ValueError('Package content differs from inventory: ' + name)
        if item['role'] in ('config', 'documentation', 'license') and SECRET.search(file.read_bytes()):
            raise ValueError('Credential marker in package text')
        expected[name] = item
    actual = {path.relative_to(directory).as_posix() for path in tree_files(directory)}
    if actual != set(expected) | META:
        raise ValueError('Package has missing or unlisted files')
    sums = ''.join(item['sha256'] + '  ' + name + '\n' for name, item in sorted(expected.items()))
    sums += digest(directory / 'release.json') + '  release.json\n'
    if (directory / 'SHA256SUMS.txt').read_text(encoding='utf-8') != sums:
        raise ValueError('Package SHA256SUMS does not match')
    if not runtime.issubset(expected):
        raise ValueError('Package is missing a required player component')
    if not set(extractor_sources(release['version'])).issubset(expected):
        raise ValueError('Package is missing PS4 extraction dependencies or instructions')
    if package_format in (2, 3) and not (set(required_data) | {'README.txt', 'SOURCE.txt', batch_name(release['version'])}).issubset(expected):
        raise ValueError('Package is missing selected data or instructions')
    if renamed(release['version']):
        primary = expected[LAUNCHER]
        if primary['role'] != 'runtime':
            raise ValueError('Launcher must be a runtime component')
        if release['flavor'] == 'Update' or package_format == 2:
            alias = expected[LEGACY_LAUNCHER]
            if alias['role'] != 'runtime' or any(alias[key] != primary[key] for key in ('bytes', 'sha256')):
                raise ValueError('Old shortcut entry must be identical to the native launcher')
        elif LEGACY_LAUNCHER in expected:
            raise ValueError('Fresh packages must use only the new launcher name')
    for name, source in guides.items():
        reject_link(source)
        item = expected.get(name)
        if (not source.is_file() or not item or item['role'] != 'documentation'
                or item['bytes'] != source.stat().st_size or item['sha256'] != digest(source)):
            raise ValueError('Missing or modified placement guide: ' + name)
    if release['flavor'] == 'Internal' and package_format == 1:
        required = []
        for key in selection:
            required += [('game-data/' + key + '/' + item['path'], item) for item in selected_data_files(games[key], release.get('dataVariants', {}).get(key))]
        for key in set(selection) & MANIFEST_ONLY_DATA:
            prefix = 'game-data/' + key + '/'
            supported = {prefix + item['path'] for item in selected_data_files(games[key], release.get('dataVariants', {}).get(key))}
            supported.update(name for name in guides if name.startswith(prefix))
            if {name for name in expected if name.startswith(prefix)} != supported:
                raise ValueError(key + ' package must contain only the supported resource manifest')
        for package in games['futari']['content']:
            required += [('game-data/futari/dlc/' + item['path'], item) for item in package['files']]
        for name, identity in required:
            item = expected.get(name)
            if not item or item['role'] != 'game-data' or any(item[key] != identity[key] for key in ('bytes', 'sha256')):
                raise ValueError('Internal package is missing supported data/DLC: ' + name)
        source = directory / 'source'
        if not (source / '.git').is_dir() or (source / '.git/objects/info/alternates').exists() or \
                git(source, 'rev-parse', 'HEAD').decode().strip() != release['sourceCommit'] or \
                git(source, 'remote', 'get-url', 'origin').decode().strip() != REMOTE or git(source, 'status', '--porcelain'):
            raise ValueError('Internal source Git identity/independence is invalid')
    return release


def archive(directory, destination, *, include_root=True):
    prefix = directory.name + '/' if include_root else ''
    with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as output:
        for file in tree_files(directory):
            output.write(file, prefix + file.relative_to(directory).as_posix())
        # An uninitialized Git submodule is an empty directory. Omitting it
        # makes an otherwise complete source checkout appear deleted after
        # extraction, even though every file and Git object matches its hash.
        for current, folders, files in os.walk(directory):
            folder = Path(current)
            if folder != directory and not folders and not files:
                reject_link(folder)
                output.write(folder, prefix + folder.relative_to(directory).as_posix() + '/')
    with zipfile.ZipFile(destination) as check:
        bad = check.testzip()
        if bad:
            raise ValueError('ZIP CRC failed: ' + bad)
    with destination.with_suffix(destination.suffix + '.sha256').open('x', encoding='ascii') as stream:
        stream.write(digest(destination) + '  ' + destination.name + '\n')


def certified_name(name):
    # These aliases are staged from the same certified PS4 host. Do not infer
    # aliases for other paths or arbitrary executable names.
    if name in {'runtime/ps4/' + game + suffix
                for game in ('ketsui', 'daioujou', 'esprade') for suffix in ('.exe', '.pdb')}:
        return 'KetsuiNativeProbe' + Path(name).suffix
    if name in {LEGACY_LAUNCHER, str(Path(LEGACY_LAUNCHER).with_suffix('.pdb'))}:
        return NAME + Path(name).suffix
    return Path(name).name


def verify_certificate(game, certified, commit):
    path = ROOT / ('runtime/games/' + game['id'] + '/runtime-manifest.ini'); reject_link(path)
    ini = configparser.RawConfigParser(interpolation=None, delimiters=('='), strict=True)
    ini.read_string(path.read_text(encoding='utf-8'))
    if ini['runtime']['contract'] != '1' or ini['runtime']['game'] != game['id'] or ini['runtime']['source_commit'] != commit:
        raise ValueError('Runtime certificate belongs to another contract/game/commit')
    expected = {}
    for name in game['runtimeFiles']:
        if name == path.relative_to(ROOT).as_posix():
            continue
        source_name = certified_name(name)
        expected[name.lower()] = certified[source_name]['sha256']
    if dict(ini['files']) != expected:
        raise ValueError('Runtime certificate file set differs from the certified build')


def build(flavor, version, output=None, data=None, futari_dlc=None, *,
          data_games=None, futari_content=None, symbols=False, package_format=3):
    package_name = version_name(version, flavor, package_format)
    selection = release_games(version)
    if flavor in ('Public', 'Update') and (data or futari_dlc or data_games or futari_content or symbols):
        raise ValueError(flavor + ' does not accept ROM or source inclusion options')
    if type(symbols) is not bool:
        raise ValueError('Symbols selection must be a boolean')
    included = selected_items(data_games if data_games is not None else (data or {}).keys(), selection, 'game data') if package_format in (2, 3) else list(selection)
    if data and set(data) - set(included):
        raise ValueError('Data directory override for an unselected game')
    if package_format in (2, 3) and ((futari_content and 'futari' not in included) or (futari_dlc and not futari_content)):
        raise ValueError('Select Futari data and DLC packages before specifying a DLC source')
    games = catalogue(selection)
    content_ids = selected_items(futari_content, [item['id'] for item in games['futari']['content']], 'Futari DLC') if package_format in (2, 3) else [item['id'] for item in games['futari']['content']]
    commit, receipts, certified = certified_runtime(selection)
    if has_ps4_extractor(version):
        ps4_extract_tools.verify(ROOT)
    for key in selection:
        verify_certificate(games[key], certified, commit)
    if data and set(data) - set(selection):
        raise ValueError('Unknown Internal data selection')
    # Resolve all mandatory inputs before creating any package output.
    inspector = certified['cave_inspect.exe']['source']
    reports = {key: inspect_game(inspector, key, (data or {}).get(key)) for key in included} if flavor == 'Internal' else {}
    if futari_dlc and 'futari' in reports:
        reports['futari'] = inspect_game(inspector, 'futari', (data or {}).get('futari'), futari_dlc)
    packages = [item for item in games['futari']['content'] if item['id'] in content_ids] if flavor == 'Internal' else []
    dlc = None
    dlc_files = {}
    if packages:
        source = futari_dlc or reports['futari'].get('contentDirectory')
        if not source:
            raise ValueError('Selected Futari DLC needs a content directory')
        dlc = Path(source)
        dlc_files = content_inputs(dlc, packages, reports['futari'])
    redist = redist_directory(); crt = sorted(redist.glob('*.dll'))
    if not {'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140_atomic_wait.dll'}.issubset({file.name.lower() for file in crt}):
        raise ValueError('App-local CRT redistributables are incomplete')
    parent = (Path(output) if output else ROOT / 'releases' /
              (dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8])).resolve()
    if not parent.is_relative_to(ROOT / 'releases'):
        raise ValueError('Release output must be a new directory below collection/releases')
    for source in [*(Path(report['dataDirectory']).resolve() for report in reports.values()), *([dlc.resolve()] if dlc else [])]:
        if parent.is_relative_to(source) or source.is_relative_to(parent):
            raise ValueError('Release output overlaps a read-only game-data source')
    reject_link(parent)
    parent.mkdir(parents=True, exist_ok=False); directory = parent / package_name; directory.mkdir()
    entries = []
    include_symbols = symbols if package_format in (2, 3) else flavor == 'Internal'
    for path in sorted(runtime_names(games, include_symbols, selection, version, flavor, package_format)):
        target = directory / path
        if path.endswith('/runtime-manifest.ini'):
            source = ROOT / path
            # The manifest's exact runtime set/hash is checked again by the player.
            copy_checked(source, target, 'runtime', entries)
            continue
        source_name = certified_name(path)
        item = certified[source_name]
        certified_launcher = path in {launcher(version), LEGACY_LAUNCHER}
        if not certified_launcher and path not in ('runtime/launcher/cave_inspect.exe', 'tools/Extract-PS4.exe') and not path.endswith('.pdb') and digest(ROOT / path) != item['sha256']:
            raise ValueError('Staged player component differs from certified build: ' + path)
        copy_checked(item['source'], target, 'runtime', entries, item)
    for file in crt:
        for subdir in ('', 'runtime/xbox360', 'runtime/ps4', 'runtime/launcher'):
            copy_checked(file, directory / subdir / file.name, 'redist', entries)
    if flavor != 'Update' and package_format == 1:
        copy_checked(ROOT / 'config/default.ini', directory / config_name(version), 'config', entries)
    if flavor != 'Update':
        for name, source in placement_sources(version).items():
            copy_checked(source, directory / name, 'documentation', entries)
    readme = ('README.player-v032.txt' if version == '0.3.2' else
              'README.player-v031.txt' if version == '0.3.1' else
              'README.player-v07.txt' if 'guwange' in selection else
              'README.player-v06.txt' if 'esprade' in selection else
              'README.player-v04.txt' if config_name(version).startswith('user-data/') else
              'README.player-v03.txt' if 'daioujou' in selection else 'README.player.txt')
    copy_checked(ROOT / 'config' / readme, directory / 'README.txt', 'documentation', entries)
    for name, source in extraction_sources(version).items():
        copy_checked(source, directory / name, 'documentation', entries)
    for name, source in extractor_sources(version).items():
        copy_checked(source, directory / name, extractor_role(name), entries)
    # Frozen pre-v0.4 package profiles retain their original executable name.
    bat_source = ROOT / 'Launch.bat'; reject_link(bat_source)
    bat = bat_source.read_text(encoding='utf-8')
    text_new(directory / batch_name(version), bat.replace(LAUNCHER, launcher(version)), 'documentation', entries)
    for file in license_files():
        copy_checked(file, directory / 'licenses' / file.relative_to(ROOT), 'license', entries)
    copy_checked(ROOT / 'launcher/artwork.json', directory / 'licenses/artwork-sources.json', 'license', entries)
    for source, license_name in (('tools/sdk-source.lock.json', 'sdk-sources.json'),
                         ('tools/ps4-source.lock.json', 'ps4-sources.json')):
        copy_checked(ROOT / source, directory / 'licenses' / license_name, 'license', entries)
    source_location = ('覆盖更新保留旧 source/ 目录，其中源码不会自动更新。继续开发前请拉取\n'
                       '上述提交，或使用本版对应源码附件。\n' if flavor == 'Update' else
                       '包内 source/ 对应同一提交。\n' if flavor == 'Internal' else
                       '本包的对应源码由同版本的独立源码附件提供。\n')
    source_text = (NAME + '\n\n对应维护源码与构建说明：\n' +
        REPOSITORY_URL + '/tree/' + commit + '\n\n' +
        source_location + '若没有仓库访问权限，请联系提供本包的人获取\n' +
        '对应版本的维护源码、依赖版本与构建说明。原始游戏数据不在源码 Git 中。\n' +
        '各组件保留原许可证，完整声明见 licenses/。\n')
    if package_format in (2, 3):
        source_text = (NAME + '\n\nSource / 对应源码：\n' + REPOSITORY_URL + '/tree/' + commit +
            '\n\nBuild instructions / 构建说明：\n' + REPOSITORY_URL + '/blob/' + commit + '/FOR_AGENT.md\n'
            '\nCheckout / 获取本版本源码：\n'
            'git clone ' + REPOSITORY_URL + '.git\n'
            'cd ' + slug(version) + '\n'
            'git checkout --detach ' + commit + '\n\n'
            'Pinned dependencies / 固定依赖：tools/sdk-source.lock.json, tools/ps4-source.lock.json\n'
            'Restore / 恢复依赖：scripts/sdk_sources.py restore; scripts/ps4_sources.py restore\n'
            'Component licenses / 组件许可证：licenses/\n')
    text_new(directory / 'SOURCE.txt', source_text, 'documentation', entries)
    if flavor == 'Internal':
        for key, report in reports.items():
            print('Copying complete data: ' + key, flush=True)
            excluded = {'dlc'} if key == 'futari' else set()
            if placement_sources(version):excluded.update((PLACEMENT_NAME.casefold(), LEGACY_PLACEMENT_NAME.casefold()))
            copy_data(Path(report['dataDirectory']), directory / 'game-data' / key, entries, selected_data_files(games[key], report.get('dataVariant')),
                      excluded, manifest_only=package_format in (2, 3) or key in MANIFEST_ONLY_DATA)
        if dlc:
            for package in packages:
                for file in package['files']:
                    copy_checked(dlc_files[file['path']], directory / 'game-data/futari/dlc' / file['path'],
                                 'game-data', entries, file)
    if flavor == 'Internal' and package_format == 1:
        source_clone(directory / 'source', commit)
        for file in tree_files(directory / 'source'):
            entries.append(dict(path=file, bytes=file.stat().st_size, sha256=digest(file), role='source'))
    payload = [dict(item, path=item['path'].relative_to(directory).as_posix()) for item in entries]
    if len({item['path'] for item in payload}) != len(payload):
        raise ValueError('Duplicate package destination')
    release = dict(format=package_format, version=version, flavor=flavor, games=list(selection), sourceCommit=commit,
        placementGuideName=PLACEMENT_NAME,
        buildSnapshots={key: record['sourceSnapshot']['sha256'] for key, record in receipts.items()},
        files=sorted(payload, key=lambda item: item['path']),
        gameplayAcceptance='Recorded separately; packaging does not perform gameplay acceptance',
        uploadsPerformed=False)
    if flavor == 'Internal':
        release['dataVariants'] = {key: report['dataVariant'] for key, report in reports.items() if games[key].get('variants')}
    if flavor == 'Update':
        release.update(baseVersion=UPDATE_BASES[version], preservedPaths=list(UPDATE_PRESERVED))
    if package_format in (2, 3):
        release.update(platform='win-x64', archiveLayout='flat', sourceIncluded=False,
                       sourceUrl=REPOSITORY_URL + '/tree/' + commit, preservedPaths=list(PROGRAM_PRESERVED),
                       dataGames=included if flavor == 'Internal' else [],
                       futariContent=content_ids if flavor == 'Internal' else [], symbolsIncluded=include_symbols)
    write_json_new(directory / 'release.json', release)
    sums = ''.join(item['sha256'] + '  ' + item['path'] + '\n' for item in release['files'])
    sums += digest(directory / 'release.json') + '  release.json\n'
    with (directory / 'SHA256SUMS.txt').open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(sums)
    verify_tree(directory, games)
    destination = parent / (package_name + '.zip')
    archive(directory, destination, include_root=package_format == 1 and flavor != 'Update')
    return dict(archive=str(destination), directory=str(directory), sha256=digest(destination), bytes=destination.stat().st_size,
                flavor=flavor, sourceCommit=commit, uploaded=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'verify'])
    parser.add_argument('--flavor', choices=['Internal', 'Public'], default='Public')
    parser.add_argument('--version', default=DEFAULT_VERSION)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--data-dir', action='append', default=[])
    parser.add_argument('--futari-dlc', type=Path)
    parser.add_argument('--data-game', action='append', choices=['all', *V07_GAMES])
    parser.add_argument('--futari-content', action='append', choices=['all', 'black-label', 'ver101'])
    parser.add_argument('--symbols', action='store_true')
    args = parser.parse_args()
    if args.action == 'verify':
        if not args.directory:
            parser.error('verify requires --directory')
        record = verify_tree(args.directory.resolve(strict=True))
        print(json.dumps(dict(version=record['version'], flavor=record['flavor'], files=len(record['files']), verified=True)))
    else:
        if not args.flavor:
            parser.error('build requires --flavor')
        data = {}
        for value in args.data_dir:
            key, separator, path = value.partition('=')
            if not separator or not path or key in data:
                parser.error('--data-dir requires unique game=path values')
            data[key] = Path(path)
        print(json.dumps(build(args.flavor, args.version, args.output, data, args.futari_dlc,
                               data_games=args.data_game, futari_content=args.futari_content, symbols=args.symbols), indent=2))


if __name__ == '__main__':
    main()
