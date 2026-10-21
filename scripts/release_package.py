"""Build and verify full releases and configuration-preserving update archives."""
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

LEGACY_GAMES = ('futari', 'espgaluda2', 'ketsui')
GAMES = (*LEGACY_GAMES, 'sdoj')
V03_GAMES = (*GAMES, 'daioujou')
DEFAULT_VERSION = '0.3'
MANIFEST_ONLY_DATA = {'sdoj', 'daioujou'}
REMOTE = 'git@github.com:zxl0714/CAVE-PC-Collection.git'
META = {'release.json', 'SHA256SUMS.txt'}
UPDATE_BASES = {'0.3.1': '0.3'}
UPDATE_PRESERVED = ('config.ini', 'user-data/', 'game-data/', 'prepared/', 'cache/', 'logs/', 'source/')


def git(root, *args):
    return subprocess.check_output(['git', '--no-optional-locks', '-C', str(root), *args], stderr=subprocess.PIPE)


def version_name(version, flavor):
    if not re.fullmatch(r'[0-9]+\.[0-9]+(?:\.[0-9]+)?(?:-[a-z0-9.-]+)?', version) or flavor not in ('Internal', 'Public', 'Update'):
        raise ValueError('Expected a numeric version and Internal/Public/Update flavor')
    if flavor == 'Update' and version not in UPDATE_BASES:
        raise ValueError('No update baseline for this release version')
    return 'CAVE-PC-Collection-v' + version + '-' + flavor.lower()


def release_games(version):
    version_name(version, 'Public')
    major, minor = map(int, version.split('-', 1)[0].split('.')[:2])
    if (major, minor) == (0, 1):
        return LEGACY_GAMES
    if (major, minor) == (0, 2):
        return GAMES
    if (major, minor) in ((0, 3), (0, 4)):
        return V03_GAMES
    raise ValueError('No game-set profile for this release version')


def catalogue(selection=GAMES):
    games = {game['id']: game for game in load(ROOT / 'games/catalogue.json')}
    if any(not games[key]['integrated'] or games[key]['comingSoon'] or not games[key]['runtimeFiles']
           or not games[key].get('communitySupported', True) for key in selection):
        raise ValueError('All selected release games must be integrated with community support: ' + ', '.join(selection))
    return games


def config_name(version):
    release_games(version)
    return 'user-data/config.ini' if version in UPDATE_BASES or version.split('-', 1)[0].split('.')[:2] == ['0', '4'] else 'config.ini'


def runtime_names(games, symbols=False, selection=GAMES):
    names = {'CAVE-PC-Collection.exe', 'runtime/launcher/cave_inspect.exe'}
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
        launcher={'CAVE-PC-Collection.exe', 'CAVE-PC-Collection.pdb', 'cave_inspect.exe', 'cave_inspect.pdb'})
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


def inspect_game(inspector, key, data=None):
    command = [str(inspector), '--root', str(ROOT), '--game', key, '--full']
    if data:
        command += ['--data-dir', str(data)]
    report = json.loads(subprocess.check_output(command))['games'][0]
    if report['dataState'] != 'verified' or report['problems']:
        raise ValueError('Complete supported data required for Internal: ' + key)
    return report


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
    paths = {ROOT / 'third_party/rexglue/LICENSE'}
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


def validate_public_name(name, games, selection=GAMES):
    allowed = runtime_names(games, selection=selection) | {'config.ini', 'user-data/config.ini', 'README.txt', '启动游戏.bat', 'SOURCE.txt'}
    if name in allowed or name.startswith('licenses/'):
        return
    # App-local CRT copies only, not arbitrary DLLs supplied by an input tree.
    parent, base = Path(name).parent.as_posix(), Path(name).name.lower()
    if parent in ('.', 'runtime/xbox360', 'runtime/ps4', 'runtime/launcher') and re.fullmatch(
            r'(?:msvcp140(?:_[12]|_atomic_wait|_codecvt_ids)?|vcruntime140(?:_1|_threads)?|concrt140|vccorlib140)\.dll', base):
        return
    raise ValueError('File is not in the Public allowlist: ' + name)


def validate_update_name(name, games, selection=V03_GAMES):
    normalized = safe_name(name).casefold()
    if any(normalized == item.rstrip('/') or normalized.startswith(item)
           for item in UPDATE_PRESERVED):
        raise ValueError('Update must preserve existing player files: ' + name)
    validate_public_name(name, games, selection)


def verify_tree(directory, games=None):
    release = json.loads((directory / 'release.json').read_text(encoding='utf-8'))
    selection = release_games(release.get('version', ''))
    games = games or catalogue(selection)
    if release.get('format') != 1 or release.get('flavor') not in ('Internal', 'Public', 'Update') or release.get('games') != list(selection):
        raise ValueError('Invalid release identity')
    if release['flavor'] == 'Update' and (release['version'] not in UPDATE_BASES
            or release.get('baseVersion') != UPDATE_BASES.get(release['version'])
            or release.get('preservedPaths') != list(UPDATE_PRESERVED)):
        raise ValueError('Invalid update baseline or preservation contract')
    expected = {}
    for item in release['files']:
        name = safe_name(item['path'])
        if name in expected or name in META:
            raise ValueError('Duplicate or recursive package inventory entry')
        if release['flavor'] in ('Public', 'Update'):
            validator = validate_update_name if release['flavor'] == 'Update' else validate_public_name
            validator(name, games, selection)
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
    if not runtime_names(games, release['flavor'] == 'Internal', selection).issubset(expected):
        raise ValueError('Package is missing a required player component')
    if release['flavor'] == 'Internal':
        required = []
        for key in selection:
            required += [('game-data/' + key + '/' + item['path'], item) for item in games[key]['files']]
        for key in set(selection) & MANIFEST_ONLY_DATA:
            prefix = 'game-data/' + key + '/'
            supported = {prefix + item['path'] for item in games[key]['files']}
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
                for game in ('ketsui', 'daioujou') for suffix in ('.exe', '.pdb')}:
        return 'KetsuiNativeProbe' + Path(name).suffix
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


def build(flavor, version, output=None, data=None, futari_dlc=None):
    package_name = version_name(version, flavor)
    selection = release_games(version)
    if flavor in ('Public', 'Update') and (data or futari_dlc):
        raise ValueError(flavor + ' does not accept ROM or source inclusion options')
    games = catalogue(selection); commit, receipts, certified = certified_runtime(selection)
    for key in selection:
        verify_certificate(games[key], certified, commit)
    if data and set(data) - set(selection):
        raise ValueError('Unknown Internal data selection')
    # Resolve all mandatory inputs before creating any package output.
    inspector = certified['cave_inspect.exe']['source']
    reports = {key: inspect_game(inspector, key, (data or {}).get(key)) for key in selection} if flavor == 'Internal' else {}
    if reports:
        dlc = Path(futari_dlc) if futari_dlc else Path(reports['futari']['contentDirectory'])
        for package in games['futari']['content']:
            for file in package['files']:
                source = dlc / file['path']
                if not source.is_file() or source.stat().st_size != file['bytes'] or digest(source) != file['sha256']:
                    raise ValueError('Internal requires complete Futari DLC: ' + package['id'])
    redist = redist_directory(); crt = sorted(redist.glob('*.dll'))
    if not {'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140_atomic_wait.dll'}.issubset({file.name.lower() for file in crt}):
        raise ValueError('App-local CRT redistributables are incomplete')
    parent = (Path(output) if output else ROOT / 'releases' /
              (dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8])).resolve()
    if not parent.is_relative_to(ROOT / 'releases'):
        raise ValueError('Release output must be a new directory below collection/releases')
    for source in [*(Path(report['dataDirectory']).resolve() for report in reports.values()), *([dlc.resolve()] if reports else [])]:
        if parent.is_relative_to(source) or source.is_relative_to(parent):
            raise ValueError('Release output overlaps a read-only game-data source')
    reject_link(parent)
    parent.mkdir(parents=True, exist_ok=False); directory = parent / package_name; directory.mkdir()
    entries = []
    for path in sorted(runtime_names(games, flavor == 'Internal', selection)):
        target = directory / path
        if path.endswith('/runtime-manifest.ini'):
            source = ROOT / path
            # The manifest's exact runtime set/hash is checked again by the player.
            copy_checked(source, target, 'runtime', entries)
            continue
        source_name = certified_name(path)
        item = certified[source_name]
        certified_launcher = flavor == 'Update' and path == 'CAVE-PC-Collection.exe'
        if not certified_launcher and path != 'runtime/launcher/cave_inspect.exe' and not path.endswith('.pdb') and digest(ROOT / path) != item['sha256']:
            raise ValueError('Staged player component differs from certified build: ' + path)
        copy_checked(item['source'], target, 'runtime', entries, item)
    for file in crt:
        for subdir in ('', 'runtime/xbox360', 'runtime/ps4', 'runtime/launcher'):
            copy_checked(file, directory / subdir / file.name, 'redist', entries)
    if flavor != 'Update':
        copy_checked(ROOT / 'config/default.ini', directory / config_name(version), 'config', entries)
    readme = ('README.player-v031.txt' if version == '0.3.1' else
              'README.player-v04.txt' if config_name(version).startswith('user-data/') else
              'README.player-v03.txt' if 'daioujou' in selection else 'README.player.txt')
    copy_checked(ROOT / 'config' / readme, directory / 'README.txt', 'documentation', entries)
    copy_checked(ROOT / '启动游戏.bat', directory / '启动游戏.bat', 'documentation', entries)
    for file in license_files():
        copy_checked(file, directory / 'licenses' / file.relative_to(ROOT), 'license', entries)
    copy_checked(ROOT / 'launcher/artwork.json', directory / 'licenses/artwork-sources.json', 'license', entries)
    for source, license_name in (('tools/sdk-source.lock.json', 'sdk-sources.json'),
                         ('tools/ps4-source.lock.json', 'ps4-sources.json')):
        copy_checked(ROOT / source, directory / 'licenses' / license_name, 'license', entries)
    source_location = ('覆盖更新保留旧 source/ 目录，其中源码不会自动更新。继续开发前请拉取\n'
                       '上述提交，或使用本版对应源码附件。\n' if flavor == 'Update' else
                       '内部包的 source/ 对应同一提交。\n')
    source_text = ('CAVE PC Collection\n\n对应维护源码与构建说明：\n' +
        'https://github.com/zxl0714/CAVE-PC-Collection/tree/' + commit + '\n\n' +
        source_location + '若没有仓库访问权限，请联系提供本包的人获取\n' +
        '对应版本的维护源码、依赖版本与构建说明。原始游戏数据不在源码 Git 中。\n' +
        '各组件保留原许可证，完整声明见 licenses/。\n')
    text_new(directory / 'SOURCE.txt', source_text, 'documentation', entries)
    if flavor == 'Internal':
        for key, report in reports.items():
            print('Copying complete data: ' + key, flush=True)
            copy_data(Path(report['dataDirectory']), directory / 'game-data' / key, entries, games[key]['files'],
                      {'dlc'} if key == 'futari' else (), manifest_only=key in MANIFEST_ONLY_DATA)
        target_dlc = directory / 'game-data/futari/dlc'
        copy_data(dlc, target_dlc, entries, [file for package in games['futari']['content'] for file in package['files']])
        source_clone(directory / 'source', commit)
        for file in tree_files(directory / 'source'):
            entries.append(dict(path=file, bytes=file.stat().st_size, sha256=digest(file), role='source'))
    payload = [dict(item, path=item['path'].relative_to(directory).as_posix()) for item in entries]
    if len({item['path'] for item in payload}) != len(payload):
        raise ValueError('Duplicate package destination')
    release = dict(format=1, version=version, flavor=flavor, games=list(selection), sourceCommit=commit,
        buildSnapshots={key: record['sourceSnapshot']['sha256'] for key, record in receipts.items()},
        files=sorted(payload, key=lambda item: item['path']),
        gameplayAcceptance='Recorded separately; packaging does not perform gameplay acceptance',
        uploadsPerformed=False)
    if flavor == 'Update':
        release.update(baseVersion=UPDATE_BASES[version], preservedPaths=list(UPDATE_PRESERVED))
    write_json_new(directory / 'release.json', release)
    sums = ''.join(item['sha256'] + '  ' + item['path'] + '\n' for item in release['files'])
    sums += digest(directory / 'release.json') + '  release.json\n'
    with (directory / 'SHA256SUMS.txt').open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(sums)
    verify_tree(directory, games)
    destination = parent / (package_name + '.zip'); archive(directory, destination, include_root=flavor != 'Update')
    return dict(archive=str(destination), directory=str(directory), sha256=digest(destination), bytes=destination.stat().st_size,
                flavor=flavor, sourceCommit=commit, uploaded=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'verify'])
    parser.add_argument('--flavor', choices=['Internal', 'Public', 'Update'])
    parser.add_argument('--version', default=DEFAULT_VERSION)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--data-dir', action='append', default=[])
    parser.add_argument('--futari-dlc', type=Path)
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
        print(json.dumps(build(args.flavor, args.version, args.output, data, args.futari_dlc), indent=2))


if __name__ == '__main__':
    main()
