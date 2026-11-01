"""Prepare an immutable, pinned standalone paraLLEl-GS build view."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

from project import ROOT, reject_link
from ps4_sources import git, locked_dependencies, verify_checkout

NAMES = ('ParallelGS', 'GraniteGS', 'VolkGS', 'VulkanHeadersGS')
PLACEMENTS = {'ParallelGS': '', 'GraniteGS': 'Granite',
              'VolkGS': 'Granite/third_party/volk',
              'VulkanHeadersGS': 'Granite/third_party/khronos/vulkan-headers'}


def digests(view):
    result = {}
    for path in sorted(view.rglob('*')):
        reject_link(path)
        if path.is_file() and path != view / 'collection-source.json':
            with path.open('rb') as source:
                result[path.relative_to(view).as_posix()] = hashlib.file_digest(source, 'sha256').hexdigest()
    return result


def prepare(output, root=ROOT):
    root, output = Path(root).absolute(), Path(output).absolute()
    reject_link(output)
    lock = {item['name']: item for item in locked_dependencies(root, 'ps2')}
    inputs = {name: root / '.deps/ps2' / name for name in NAMES}
    for name, path in inputs.items(): verify_checkout(path, lock[name])
    # Submodule pins must agree with the parent projects, even though sources
    # are restored independently through the collection's shared workflow.
    for parent, path, child in (
        ('ParallelGS', 'Granite', 'GraniteGS'),
        ('GraniteGS', 'third_party/volk', 'VolkGS'),
        ('GraniteGS', 'third_party/khronos/vulkan-headers', 'VulkanHeadersGS'),
    ):
        if git(inputs[parent], 'rev-parse', 'HEAD:' + path) != lock[child]['revision']:
            raise ValueError(f'Inconsistent GS dependency pin: {parent}/{path}')
    receipt = {'format': 2, 'revisions': {name: lock[name]['revision'] for name in NAMES}}
    view = output / ('source-v2-' + '-'.join(receipt['revisions'][n][:10] for n in NAMES))
    stamp = view / 'collection-source.json'
    reject_link(view)
    if stamp.exists():
        existing = json.loads(stamp.read_text(encoding='utf-8'))
        if ({k: existing.get(k) for k in receipt} != receipt
                or existing.get('files') != digests(view)):
            raise ValueError('Existing GS source view differs; files preserved')
        return view
    if view.exists():
        raise ValueError('Incomplete GS source view exists; files preserved')
    for name in NAMES:
        destination = view / PLACEMENTS[name]
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(inputs[name], destination, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('.git'))
    receipt['files'] = digests(view)
    stamp.write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    return view


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(prepare(args.output).as_posix())
