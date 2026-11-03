"""Stage the certified PS2 player runtime and its matching private symbols."""
import json
import shutil
import uuid
from pathlib import Path

from project import ROOT, digest, reject_link, write_json_new
from ps2_build_receipt import verify
from stage_ps4 import write_manifest


def stage():
    return _stage_verified(verify())


def _stage_verified(receipt):
    """Use the same-process finished snapshot; still recheck every copied byte."""
    outputs = []
    for item in receipt['outputs']:
        source = ROOT / item['path']
        target = ROOT / 'runtime/ps2' / source.name
        reject_link(source)
        reject_link(target)
        if digest(source) != item['sha256']:
            raise ValueError('PS2 source differs from certified output')
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.is_file() or digest(target) != item['sha256']:
            if target.exists():
                backup = ROOT / '.local/staging' / ('ps2-' + uuid.uuid4().hex + '-' + target.name)
                backup.parent.mkdir(parents=True, exist_ok=True)
                target.rename(backup)
            shutil.copy2(source, target)
        if digest(target) != item['sha256']:
            raise ValueError('PS2 runtime copy failed verification')
        outputs.append(dict(path=target.relative_to(ROOT).as_posix(), bytes=item['bytes'], sha256=item['sha256']))
    games = receipt.get('games', ['espgaluda'])
    for game in games:
        selected = [item for item in outputs if Path(item['path']).stem in (game, 'SDL3')]
        write_manifest(game, receipt, selected)
    return dict(games=games, stagedFiles=len(outputs) + 2*len(games), gameDataIncluded=False)


if __name__ == '__main__':
    print(json.dumps(stage(), indent=2))
