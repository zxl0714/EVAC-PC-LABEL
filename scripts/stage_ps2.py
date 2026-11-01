"""Stage the certified PS2 player runtime and its matching private symbols."""
import json
from pathlib import Path
import shutil
import uuid

from project import ROOT, digest, reject_link, write_json_new
from ps2_build_receipt import verify
from stage_ps4 import write_manifest


def stage():
    receipt = verify()
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
    write_manifest('espgaluda', receipt, outputs)
    return dict(games=['espgaluda'], stagedFiles=len(outputs) + 2, gameDataIncluded=False)


if __name__ == '__main__':
    print(json.dumps(stage(), indent=2))
