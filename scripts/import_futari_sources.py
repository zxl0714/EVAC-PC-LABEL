"""Import only pinned Futari maintenance source, preserving both existing trees."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from project import ROOT, digest, reject_link, write_json_new

BASELINE = '7dced5979076329db71b1296c81944c83fb72b48'
REPOSITORY = 'https://github.com/zxl0714/Mushihimesama-Futari-PC'


def destination(name):
    prefix = 'project-v0.9/'
    if name.startswith(prefix) and '/' not in name[len(prefix):] and name.endswith('.toml'):
        leaf = name[len(prefix):]
        if leaf == 'mushihimesama_futari_manifest.toml':
            leaf = 'futari.toml'
        return 'games/futari/recomp/' + leaf
    if name.startswith(prefix + 'src/'):
        leaf = name[len(prefix + 'src/'):]
        if '/' in leaf or not leaf.endswith(('.cpp', '.h')):
            return None
        if leaf.startswith('audio_'):
            folder = 'audio'
        elif leaf.startswith(('community_', 'replay_format.')):
            folder = 'community'
        elif leaf.startswith(('futari_diagnostics.', 'diagnostics_shared.', 'storage_diagnostics.', 'crash_writer.')):
            folder = 'diagnostics'
        elif leaf == 'module_switch_compat.cpp':
            folder = 'compat'
        elif leaf == 'keyboard_tap_test.cpp':
            folder = 'tools'
        else:
            # Do not import the old host, graphical settings or another SDK.
            return None
        return 'games/futari/' + folder + '/' + leaf
    if name in {'tests/audio-guest-trace-test.cpp', 'tests/audio-telemetry-test.cpp',
                'tests/audio-wait-policy-test.cpp', 'tests/community-client-test.cpp'}:
        return 'tests/futari/' + name.removeprefix('tests/')
    return None


def git(reference, *args):
    return subprocess.check_output(['git', '--no-optional-locks', '-C', str(reference), *args])


def migrate(reference, report, root=ROOT):
    reference, root, report = Path(reference).resolve(), Path(root).absolute(), Path(report).absolute()
    reject_link(root); reject_link(report)
    root, report = root.resolve(), report.resolve()
    if root.is_relative_to(reference) or reference.is_relative_to(root):
        raise ValueError('Source and collection must be separate trees')
    if not report.is_relative_to(root / '.local') or report.exists():
        raise ValueError('Use a new report under collection .local')
    if git(reference, 'rev-parse', 'HEAD').decode().strip() != BASELINE:
        raise ValueError('Use the accepted Futari baseline commit')
    status = git(reference, 'status', '--porcelain=v1', '-z', '--untracked-files=all')
    if status:
        raise ValueError('Preserve uncommitted reference changes before import')
    records = []
    for raw in git(reference, 'ls-tree', '-r', '-z', BASELINE).split(b'\0'):
        if not raw:
            continue
        metadata, name = raw.decode('utf-8').split('\t', 1)
        mapped = destination(name)
        if mapped is None:
            continue
        mode, kind, oid = metadata.split(' ')
        if mode not in ('100644', '100755') or kind != 'blob':
            raise ValueError('Only ordinary maintenance source may be imported')
        source, target = reference / name, root / mapped
        reject_link(source); reject_link(target)
        if target.exists():
            raise FileExistsError(target)
        data = source.read_bytes()
        if hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() != oid:
            raise ValueError('Reference differs from committed Git blob')
        data.decode('utf-8-sig')
        records.append(dict(source=name, path=mapped, bytes=len(data), sha256=digest(source)))
    if not records:
        raise ValueError('No selected maintenance source')
    # All selections are checked before any output. Exclusive writes never
    # replace an existing target; a failed copy leaves recoverable evidence.
    for item in records:
        source, target = reference / item['source'], root / item['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        with source.open('rb') as reader, target.open('xb') as writer:
            writer.write(reader.read())
        if digest(source) != item['sha256'] or digest(target) != item['sha256']:
            raise ValueError('Source changed during import; preserve partial output')
    if status != git(reference, 'status', '--porcelain=v1', '-z', '--untracked-files=all'):
        raise ValueError('Reference state changed during import')
    write_json_new(report, dict(format=1, repository=REPOSITORY, commit=BASELINE,
                               files=records, sourceUnchanged=True, gameDataIncluded=False))
    return dict(files=len(records), bytes=sum(item['bytes'] for item in records),
                sourceUnchanged=True, gameDataIncluded=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(migrate(args.reference, args.report), indent=2))
