"""Import the pinned Ketsui runtime and maintained GPU components, never game data."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess

from project import ROOT, SECRET, TEXT, digest, reject_link, write_json_new

BASELINE = '9501378a609d39f20e1d72d41664986ceb167f99'
REPOSITORY = 'https://github.com/zxl0714/Ketsui-PC'
SPECIAL = {
    'CMakeLists.txt': 'backends/ps4/CMakeLists.txt',
    'THIRD_PARTY_NOTICES.txt': 'third_party/ps4/THIRD_PARTY_NOTICES.txt',
    'config/gamecontrollerdb.txt': 'games/ketsui/gamecontrollerdb.txt',
    'scripts/native_elf.py': 'scripts/ps4/native_elf.py',
    'scripts/prepare-native.py': 'scripts/ps4/prepare_native.py',
    'scripts/inspect-native-bindings.py': 'scripts/ps4/inspect_bindings.py',
    'scripts/analyze-native-timing.py': 'scripts/ps4/analyze_timing.py',
    'scripts/game_fixes.py': 'scripts/ketsui/game_fixes.py',
    'scripts/prepare-game-fixes.py': 'scripts/ketsui/prepare_fixes.py',
    'server/ketsui_live.py': 'server/games/ketsui.py',
}


def destination(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or ':' in name or '\\' in name:
        return None
    if name in SPECIAL:
        return SPECIAL[name]
    if name.startswith('src/') and not name.startswith('src/launcher/'):
        if path.suffix in {'.cpp', '.h', '.hpp', '.s'}:
            return 'backends/ps4/' + name.removeprefix('src/')
    if name.startswith('third_party/'):
        if path.parts[1] in {'gcn', 'gpu_core', 'audio', 'community', 'input'}:
            return 'third_party/ps4/' + name.removeprefix('third_party/')
    if name.startswith('LICENSES/') and len(path.parts) == 2 and path.suffix == '.txt':
        return 'third_party/ps4/' + name
    # Keep the original source-level regressions for adaptation after the native
    # build is wired. Old launcher/release/toolchain tests are not collection tests.
    if name.startswith('tests/test_') and len(path.parts) == 2:
        if path.name in {'test_native.py', 'test_atrac9.py', 'test_input_config.py',
                         'test_gamepad.py', 'test_host_timing.py', 'test_game_fixes.py',
                         'test_timing_analysis.py', 'test_import_analysis.py',
                         'test_community.py', 'test_community_client.py',
                         'test_community_replay.py', 'test_community_rounds.py',
                         'test_community_round_worker.py', 'test_save_recovery.py',
                         'test_gpu_compile_metrics.py'}:
            return 'tests/ketsui/' + path.name
    return None


def git(reference, *args):
    return subprocess.check_output(['git', '--no-optional-locks', '-C', str(reference), *args])


def migrate(reference, report, root=ROOT):
    reference, root, report = Path(reference).absolute(), Path(root).absolute(), Path(report).absolute()
    for path in (reference, root, report):
        reject_link(path)
    reference, root, report = reference.resolve(), root.resolve(), report.resolve()
    if root.is_relative_to(reference) or reference.is_relative_to(root):
        raise ValueError('Source and collection must be separate trees')
    if not report.is_relative_to(root / '.local') or report.exists():
        raise ValueError('Use a new report under collection .local')
    if git(reference, 'rev-parse', 'HEAD').decode().strip() != BASELINE:
        raise ValueError('Use the accepted Ketsui baseline commit')
    status = git(reference, 'status', '--porcelain=v1', '-z', '--untracked-files=all')
    if status:
        raise ValueError('Preserve uncommitted reference changes before import')
    records = []
    targets = set()
    for raw in git(reference, 'ls-tree', '-r', '-z', BASELINE).split(b'\0'):
        if not raw:
            continue
        metadata, name = raw.decode('utf-8').split('\t', 1)
        mapped = destination(name)
        if mapped is None:
            continue
        mode, kind, oid = metadata.split(' ')
        if mode not in {'100644', '100755'} or kind != 'blob':
            raise ValueError('Only ordinary maintenance source may be imported')
        source, target = reference / name, root / mapped
        reject_link(source)
        reject_link(target)
        if target.exists() or mapped.casefold() in targets:
            raise FileExistsError(target)
        targets.add(mapped.casefold())
        data = source.read_bytes()
        if hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() != oid:
            raise ValueError('Reference differs from committed Git blob')
        if (len(data) > 5 * 1024 * 1024 or source.suffix.lower() not in TEXT
                or data.startswith((b'MZ', b'XEX2', b'\x7fELF', b'CON ', b'LIVE', b'PIRS'))
                or SECRET.search(data)):
            raise ValueError('Only text maintenance source is accepted')
        data.decode('utf-8-sig')
        records.append(dict(source=name, path=mapped, bytes=len(data),
                            sha256=hashlib.sha256(data).hexdigest()))
    if not records:
        raise ValueError('No selected maintenance source')
    # Preflight every selection before copying; exclusive creation preserves any
    # existing files and leaves a partial import inspectable on an IO failure.
    for item in records:
        source, target = reference / item['source'], root / item['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        with source.open('rb') as reader, target.open('xb') as writer:
            writer.write(reader.read())
        if digest(source) != item['sha256'] or digest(target) != item['sha256']:
            raise ValueError('Source changed during import; preserve partial output')
    if (git(reference, 'rev-parse', 'HEAD').decode().strip() != BASELINE
            or status != git(reference, 'status', '--porcelain=v1', '-z', '--untracked-files=all')):
        raise ValueError('Reference state changed during import')
    write_json_new(report, dict(format=1, repository=REPOSITORY, commit=BASELINE,
                               files=records, sourceUnchanged=True, gameDataIncluded=False))
    return dict(files=len(records), bytes=sum(item['bytes'] for item in records),
                sourceUnchanged=True, gameDataIncluded=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(migrate(args.reference, args.report), indent=2))
