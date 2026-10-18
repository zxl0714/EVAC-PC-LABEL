"""One-time, non-overwriting migration of the accepted G2 source baseline."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from project import ROOT, digest, reject_link, write_json_new

BASELINE = 'ba85ac0c92402e9da9c809ef15ffa2d34bab2f6a'


def destination(name):
    exact = {
        '.gitmodules': '.gitmodules',
        'tools/sdk-source.lock.json': 'tools/sdk-source.lock.json',
        'src/main.cpp': 'games/espgaluda2/host.cpp',
        'config/game-inputs.json': 'games/espgaluda2/inputs.json',
        'config/default.ini': 'tests/fixtures/g2-default.ini',
        'cmake/GameTargets.cmake': 'games/espgaluda2/overlays.cmake',
        'server/community.py': 'server/games/espgaluda2.py',
        'docs/PROGRAM_LAYOUT.md': 'games/espgaluda2/docs/PROGRAM_LAYOUT.md',
        'docs/TEST_MATRIX.md': 'games/espgaluda2/docs/BASELINE_TESTS.md',
    }
    if name in exact:
        return exact[name]
    for prefix, target in (
        ('third_party/rexglue/', 'third_party/rexglue/'),
        ('src/community/http_client.', 'common/community/http_client.'),
        ('src/community/', 'games/espgaluda2/community/'),
        ('src/config/', 'backends/xbox360/config/'),
        ('src/input/', 'backends/xbox360/input/'),
        ('src/diagnostics/', 'backends/xbox360/diagnostics/'),
        ('src/platform/', 'backends/xbox360/platform/'),
        ('src/tools/', 'games/espgaluda2/tools/'),
        ('recomp/', 'games/espgaluda2/recomp/'),
    ):
        if name.startswith(prefix):
            return target + name[len(prefix):]
    if name.startswith('tests/') and name.endswith('.cpp'):
        return 'tests/xbox360/' + name[len('tests/'):]
    return None


def git(reference, *args):
    return subprocess.check_output(['git', '--no-optional-locks', '-C', str(reference), *args])


def migrate(reference, report):
    reference = Path(reference).resolve()
    if git(reference, 'rev-parse', 'HEAD').decode().strip() != BASELINE:
        raise ValueError('Use the accepted G2 baseline commit')
    state = git(reference, 'status', '--porcelain=v1', '-z', '--untracked-files=all')
    if state:
        raise ValueError('G2 reference must be clean; preserve uncommitted changes')
    if report.exists():
        raise FileExistsError(report)
    records, links = [], []
    for raw in git(reference, 'ls-tree', '-r', '-z', BASELINE).split(b'\0'):
        if not raw:
            continue
        metadata, name = raw.decode('utf-8').split('\t', 1)
        mode, kind, oid = metadata.split(' ')
        mapped = destination(name)
        if mapped is None:
            continue
        if mode == '160000':
            links.append(dict(path=mapped, commit=oid))
            continue
        if mode not in ('100644', '100755'):
            raise ValueError('Unsupported source file mode')
        source, target = reference / name, ROOT / mapped
        reject_link(source); reject_link(target)
        if not target.resolve().is_relative_to(ROOT) or target.exists():
            raise ValueError(f'Preserve existing migration target: {mapped}')
        data = source.read_bytes()
        # Prove selection against the committed Git tree, not an arbitrary cache.
        if hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() != oid:
            raise ValueError('Reference source differs from accepted Git blob')
        data.decode('utf-8-sig')
        records.append(dict(source=name, path=mapped, bytes=len(data), sha256=digest(source)))
    if len(records) < 1000 or len(links) != 22:
        raise ValueError('Incomplete SDK/source selection')
    for record in records:
        source, target = reference / record['source'], ROOT / record['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        with source.open('rb') as reader, target.open('xb') as writer:
            shutil.copyfileobj(reader, writer)
        if digest(target) != record['sha256'] or digest(source) != record['sha256']:
            raise ValueError('Migration copy mismatch; preserve partial output')
    for link in links:
        subprocess.run(['git', '-C', str(ROOT), 'update-index', '--add', '--cacheinfo',
                        '160000,' + link['commit'] + ',' + link['path']], check=True)
    if state != git(reference, 'status', '--porcelain=v1', '-z', '--untracked-files=all'):
        raise ValueError('Reference changed during migration')
    write_json_new(report, dict(format=1, repository='https://github.com/zxl0714/ESPGaluda2-PC',
                               commit=BASELINE, files=records, submodules=links,
                               gameDataIncluded=False, sourceUnchanged=True))
    return dict(files=len(records), submodules=len(links), bytes=sum(r['bytes'] for r in records),
                gameDataIncluded=False, sourceUnchanged=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(migrate(args.reference, args.report), indent=2))
