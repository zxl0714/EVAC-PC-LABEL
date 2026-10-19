"""Build and verify local Internal/Public archives from the same certified build."""
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

GAMES = ('futari', 'espgaluda2', 'ketsui')
XBOX = ('espgaluda2', 'futari')
REMOTE = 'git@github.com:zxl0714/CAVE-PC-Collection.git'
META = {'release.json', 'SHA256SUMS.txt'}


def git(root, *args):
    return subprocess.check_output(['git', '--no-optional-locks', '-C', str(root), *args], stderr=subprocess.PIPE)


def version_name(version, flavor):
    if not re.fullmatch(r'[0-9]+\.[0-9]+(?:\.[0-9]+)?(?:-[a-z0-9.-]+)?', version) or flavor not in ('Internal', 'Public'):
        raise ValueError('Expected a numeric version and Internal/Public flavor')
    return 'CAVE-PC-Collection-v' + version + '-' + flavor.lower()


def catalogue():
    games = {game['id']: game for game in load(ROOT / 'games/catalogue.json')}
    if any(not games[key]['integrated'] or not games[key]['runtimeFiles'] for key in GAMES):
        raise ValueError('All three release games must be integrated')
    return games


def runtime_names(games, symbols=False):
    names = {'CAVE-PC-Collection.exe', 'runtime/launcher/cave_inspect.exe'}
    for key in GAMES:
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
            reject_link(Path(directory) / name)
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


def certified_runtime():
    if git(ROOT, 'status', '--porcelain', '--untracked-files=normal'):
        raise ValueError('Commit source changes before release packaging')
    verify_sdk()
    records = dict(xbox360=verify_receipt('host', XBOX), ps4=verify_ps4(), launcher=verify_launcher())
    commit = git(ROOT, 'rev-parse', 'HEAD').decode().strip()
    if any(record['sourceCommit'] != commit for record in records.values()):
        raise ValueError('Rebuild all player components at the release commit')
    audit_source()
    games = catalogue()
    xbox_names = {Path(name).name for key in XBOX for name in games[key]['runtimeFiles'] if name.endswith(('.exe', '.dll'))}
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


def copy_data(source, target, entries, required=(), exclude_top=()):
    source = source.resolve(strict=True)
    expected = {item['path']: item for item in required}
    copied = set()
    for file in tree_files(source):
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


def validate_public_name(name, games):
    allowed = runtime_names(games) | {'config.ini', 'README.txt', '启动游戏.bat', 'SOURCE.txt'}
    if name in allowed or name.startswith('licenses/'):
        return
    # App-local CRT copies only, not arbitrary DLLs supplied by an input tree.
    parent, base = Path(name).parent.as_posix(), Path(name).name.lower()
    if parent in ('.', 'runtime/xbox360', 'runtime/ps4', 'runtime/launcher') and re.fullmatch(
            r'(?:msvcp140(?:_[12]|_atomic_wait|_codecvt_ids)?|vcruntime140(?:_1|_threads)?|concrt140|vccorlib140)\.dll', base):
        return
    raise ValueError('File is not in the Public allowlist: ' + name)


def verify_tree(directory, games=None):
    games = games or catalogue()
    release = json.loads((directory / 'release.json').read_text(encoding='utf-8'))
    if release.get('format') != 1 or release.get('flavor') not in ('Internal', 'Public') or release.get('games') != list(GAMES):
        raise ValueError('Invalid release identity')
    expected = {}
    for item in release['files']:
        name = safe_name(item['path'])
        if name in expected or name in META:
            raise ValueError('Duplicate or recursive package inventory entry')
        if release['flavor'] == 'Public':
            validate_public_name(name, games)
            if item['role'] not in ('runtime', 'config', 'documentation', 'license', 'redist'):
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
    if not runtime_names(games, release['flavor'] == 'Internal').issubset(expected):
        raise ValueError('Package is missing a required player component')
    if release['flavor'] == 'Internal':
        required = []
        for key in GAMES:
            required += [('game-data/' + key + '/' + item['path'], item) for item in games[key]['files']]
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


def archive(directory, destination):
    with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as output:
        for file in tree_files(directory):
            output.write(file, directory.name + '/' + file.relative_to(directory).as_posix())
        # An uninitialized Git submodule is an empty directory. Omitting it
        # makes an otherwise complete source checkout appear deleted after
        # extraction, even though every file and Git object matches its hash.
        for current, folders, files in os.walk(directory):
            folder = Path(current)
            if folder != directory and not folders and not files:
                reject_link(folder)
                output.write(folder, directory.name + '/' + folder.relative_to(directory).as_posix() + '/')
    with zipfile.ZipFile(destination) as check:
        bad = check.testzip()
        if bad:
            raise ValueError('ZIP CRC failed: ' + bad)
    with destination.with_suffix(destination.suffix + '.sha256').open('x', encoding='ascii') as stream:
        stream.write(digest(destination) + '  ' + destination.name + '\n')


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
        source_name = Path(name).name
        if name == 'runtime/ps4/ketsui.exe':
            source_name = 'KetsuiNativeProbe.exe'
        expected[name.lower()] = certified[source_name]['sha256']
    if dict(ini['files']) != expected:
        raise ValueError('Runtime certificate file set differs from the certified build')


def build(flavor, version, output=None, data=None, futari_dlc=None):
    package_name = version_name(version, flavor)
    if flavor == 'Public' and (data or futari_dlc):
        raise ValueError('Public does not accept ROM or source inclusion options')
    games = catalogue(); commit, receipts, certified = certified_runtime()
    for key in GAMES:
        verify_certificate(games[key], certified, commit)
    if data and set(data) - set(GAMES):
        raise ValueError('Unknown Internal data selection')
    # Resolve all mandatory inputs before creating any package output.
    inspector = certified['cave_inspect.exe']['source']
    reports = {key: inspect_game(inspector, key, (data or {}).get(key)) for key in GAMES} if flavor == 'Internal' else {}
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
    for path in sorted(runtime_names(games, flavor == 'Internal')):
        target = directory / path
        if path.endswith('/runtime-manifest.ini'):
            source = ROOT / path
            # The manifest's exact runtime set/hash is checked again by the player.
            copy_checked(source, target, 'runtime', entries)
            continue
        source_name = Path(path).name
        if path.startswith('runtime/ps4/ketsui.'):
            source_name = 'KetsuiNativeProbe' + Path(path).suffix
        item = certified[source_name]
        if path != 'runtime/launcher/cave_inspect.exe' and not path.endswith('.pdb') and digest(ROOT / path) != item['sha256']:
            raise ValueError('Staged player component differs from certified build: ' + path)
        copy_checked(item['source'], target, 'runtime', entries, item)
    for file in crt:
        for subdir in ('', 'runtime/xbox360', 'runtime/ps4', 'runtime/launcher'):
            copy_checked(file, directory / subdir / file.name, 'redist', entries)
    copy_checked(ROOT / 'config/default.ini', directory / 'config.ini', 'config', entries)
    copy_checked(ROOT / 'config/README.player.txt', directory / 'README.txt', 'documentation', entries)
    copy_checked(ROOT / '启动游戏.bat', directory / '启动游戏.bat', 'documentation', entries)
    for file in license_files():
        copy_checked(file, directory / 'licenses' / file.relative_to(ROOT), 'license', entries)
    copy_checked(ROOT / 'launcher/artwork.json', directory / 'licenses/artwork-sources.json', 'license', entries)
    for source, license_name in (('tools/sdk-source.lock.json', 'sdk-sources.json'),
                         ('tools/ps4-source.lock.json', 'ps4-sources.json')):
        copy_checked(ROOT / source, directory / 'licenses' / license_name, 'license', entries)
    source_text = ('CAVE PC Collection\n\n对应维护源码与构建说明：\n' +
        'https://github.com/zxl0714/CAVE-PC-Collection/tree/' + commit + '\n\n' +
        '内部包的 source/ 对应同一提交。若没有仓库访问权限，请联系提供本包的人获取\n' +
        '对应版本的维护源码、依赖版本与构建说明。原始游戏数据不在源码 Git 中。\n' +
        '各组件保留原许可证，完整声明见 licenses/。\n')
    text_new(directory / 'SOURCE.txt', source_text, 'documentation', entries)
    if flavor == 'Internal':
        for key, report in reports.items():
            print('Copying complete data: ' + key, flush=True)
            copy_data(Path(report['dataDirectory']), directory / 'game-data' / key, entries, games[key]['files'],
                      {'dlc'} if key == 'futari' else ())
        target_dlc = directory / 'game-data/futari/dlc'
        copy_data(dlc, target_dlc, entries, [file for package in games['futari']['content'] for file in package['files']])
        source_clone(directory / 'source', commit)
        for file in tree_files(directory / 'source'):
            entries.append(dict(path=file, bytes=file.stat().st_size, sha256=digest(file), role='source'))
    payload = [dict(item, path=item['path'].relative_to(directory).as_posix()) for item in entries]
    if len({item['path'] for item in payload}) != len(payload):
        raise ValueError('Duplicate package destination')
    release = dict(format=1, version=version, flavor=flavor, games=list(GAMES), sourceCommit=commit,
        buildSnapshots={key: record['sourceSnapshot']['sha256'] for key, record in receipts.items()},
        files=sorted(payload, key=lambda item: item['path']),
        gameplayAcceptance='Recorded separately; packaging does not perform gameplay acceptance',
        uploadsPerformed=False)
    write_json_new(directory / 'release.json', release)
    sums = ''.join(item['sha256'] + '  ' + item['path'] + '\n' for item in release['files'])
    sums += digest(directory / 'release.json') + '  release.json\n'
    with (directory / 'SHA256SUMS.txt').open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(sums)
    verify_tree(directory, games)
    destination = parent / (package_name + '.zip'); archive(directory, destination)
    return dict(archive=str(destination), directory=str(directory), sha256=digest(destination), bytes=destination.stat().st_size,
                flavor=flavor, sourceCommit=commit, uploaded=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'verify'])
    parser.add_argument('--flavor', choices=['Internal', 'Public'])
    parser.add_argument('--version', default='0.1')
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
