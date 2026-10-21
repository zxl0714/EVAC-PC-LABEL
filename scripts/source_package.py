"""Package corresponding source and pinned dependencies for a binary release."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

import ps4_sources
import release_package as release
import sdk_sources
from project import ROOT, audit_source, digest, reject_link, write_json_new


def source_name(version):
    release.release_games(version)
    return release.slug(version) + '-v' + version + '-source'


def check_record(record):
    if (record.get('format') != 1 or record.get('gameDataIncluded') is not False
            or not re.fullmatch('[0-9a-f]{40}', record.get('sourceCommit', ''))
            or not re.fullmatch('[0-9a-f]{64}', record.get('binaryReleaseSha256', ''))
            or record.get('binaryFlavor') not in ('Internal', 'Public', 'Update')
            or not isinstance(record.get('files'), list)):
        raise ValueError('Invalid source release identity')
    prefix = source_name(record.get('version', '')) + '/'
    expected = {}
    for item in record['files']:
        name = release.safe_name(item['path'])
        # Only the independently cloned source and its instructions belong here.
        # Local game data/tools/output are absent from the clone, not copied and
        # subsequently deleted. Fixed PS4 dependencies are the sole .deps tree.
        if (name != 'README.txt' and not name.startswith('source/')) or name in expected:
            raise ValueError('Invalid or duplicate source inventory path')
        if name.startswith('source/'):
            relative = name[len('source/'):]
            first = relative.split('/', 1)[0].casefold()
            if first in {'.local', 'game-data', 'generated', 'prepared', 'runtime', 'out',
                         'releases', 'logs', 'cache', 'user-data'} or relative == 'config.ini':
                raise ValueError('Local game/output files in source inventory')
            if first == '.deps' and not relative.startswith('.deps/ps4/'):
                raise ValueError('Unexpected dependency tree in source inventory')
            if relative.startswith(('tools/apps/', 'tools/downloads/', 'tools/state/')):
                raise ValueError('Installed tools in source inventory')
        if (type(item.get('bytes')) is not int or item['bytes'] < 0
                or not re.fullmatch('[0-9a-f]{64}', item.get('sha256', ''))):
            raise ValueError('Invalid source file identity')
        expected[name] = item
    if not {'README.txt', 'source/.git/HEAD', 'source/FOR_AGENT.md',
            'source/tools/sdk-source.lock.json', 'source/tools/ps4-source.lock.json'}.issubset(expected):
        raise ValueError('Missing source Git or development instructions')
    return prefix, expected


def verify(archive):
    reject_link(archive)
    with zipfile.ZipFile(archive) as package:
        members = package.infolist()
        names = [item.filename for item in members]
        if len(names) != len(set(names)):
            raise ValueError('Duplicate source ZIP member')
        manifests = [name for name in names if name.endswith('/source-release.json')]
        if len(manifests) != 1:
            raise ValueError('Expected one source release manifest')
        record = json.loads(package.read(manifests[0]))
        prefix, expected = check_record(record)
        if manifests[0] != prefix + 'source-release.json':
            raise ValueError('Source ZIP root differs from its version')
        for item in members:
            release.safe_name(item.filename.rstrip('/'))
            if not item.filename.startswith(prefix):
                raise ValueError('Source ZIP member outside its root')
        actual = {item.filename for item in members if not item.is_dir()}
        if actual != {prefix + name for name in expected} | {manifests[0]}:
            raise ValueError('Source ZIP member set differs')
        for name, identity in expected.items():
            member = package.getinfo(prefix + name)
            if member.file_size != identity['bytes']:
                raise ValueError('Source ZIP size differs: ' + name)
            with package.open(member) as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != identity['sha256']:
                    raise ValueError('Source ZIP content differs: ' + name)
    return dict(sourceZipVerified=True, files=len(expected), version=record['version'],
                sourceCommit=record['sourceCommit'], gameDataIncluded=False)


def build(binary_directory, output):
    # Corresponding source is bound to an already assembled, verified binary
    # release. This entry point neither marks gameplay accepted nor uploads it.
    binary_directory = Path(binary_directory).absolute()
    reject_link(binary_directory)
    binary = release.verify_tree(binary_directory)
    commit = release.git(ROOT, 'rev-parse', 'HEAD').decode().strip()
    if (release.git(ROOT, 'status', '--porcelain', '--untracked-files=normal').strip()
            or commit != binary['sourceCommit']):
        raise ValueError('Clean source must match the binary release commit')
    audit_source(ROOT)
    output = Path(output).absolute()
    reject_link(output)
    if not output.resolve().is_relative_to(ROOT / 'releases'):
        raise ValueError('Source output must be a new directory below collection/releases')
    directory = output / source_name(binary['version'])
    output.mkdir(parents=True, exist_ok=False)
    directory.mkdir()
    source = directory / 'source'
    release.source_clone(source, commit)
    sdk_count = sdk_sources.restore(source, cache=ROOT)
    ps4 = ps4_sources.restore(source, cache=ROOT / '.deps/ps4')
    if release.git(source, 'status', '--porcelain', '--untracked-files=normal').strip():
        raise ValueError('Restored source is dirty')
    audit_source(source)
    instructions = (release.NAME + ' v' + binary['version'] + ' — 对应源码\n\n'
        '源码提交：' + commit + '\n'
        'source/ 是独立 Git 仓库，包含固定 SDK 和 PS4 依赖源码。\n'
        '不包含 ROM、生成的游戏程序正文、已安装工具、配置或存档。\n'
        '完整解压到较短的英文目录，从 source/FOR_AGENT.md 开始。\n'
        '在 source/ 执行 scripts/Prepare-Tools.ps1 准备固定工具。\n'
        '编译 Xbox 360 作品前先执行 scripts/Build-Sdk.ps1；PS4 作品不需要这一步。\n'
        '提供对应数据后，执行 scripts/Build.ps1 -Games <游戏ID> '
        '-DataDirectory <数据目录> -Jobs 2。\n'
        '仅编译公共组件和启动器不需要 ROM 或先编译 SDK。\n'
        '各组件的许可证保留在源码内。\n')
    release.text_new(directory / 'README.txt', instructions, 'documentation', [])
    files = [dict(path=file.relative_to(directory).as_posix(), bytes=file.stat().st_size,
                  sha256=digest(file)) for file in release.tree_files(directory)]
    record = dict(format=1, version=binary['version'], sourceCommit=commit, gameDataIncluded=False,
                  binaryFlavor=binary['flavor'], binaryReleaseSha256=digest(binary_directory / 'release.json'),
                  sdkDependencies=sdk_count, ps4Dependencies=ps4['verifiedDependencies'], files=files)
    check_record(record)
    write_json_new(directory / 'source-release.json', record)
    archive = output / (directory.name + '.zip')
    release.archive(directory, archive)
    result = verify(archive)
    return dict(result, archive=str(archive), bytes=archive.stat().st_size, sha256=digest(archive), uploaded=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'verify'])
    parser.add_argument('--binary-directory', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    if args.action == 'build':
        if not args.binary_directory or not args.output:
            parser.error('build requires --binary-directory and --output')
        result = build(args.binary_directory, args.output)
    else:
        if not args.archive:
            parser.error('verify requires --archive')
        result = verify(args.archive)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
