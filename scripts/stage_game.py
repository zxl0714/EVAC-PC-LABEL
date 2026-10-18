"""Stage verified player runtime files only; never copy game data or profiles."""
import json
from pathlib import Path
import shutil
import uuid

from build_receipt import output_root, verify_receipt, verify_sdk
from generate_catalogue import load
from project import ROOT, digest, reject_link, write_json_new


def stage():
    verify_sdk()
    receipt=verify_receipt('host')
    game=next(item for item in load(ROOT/'games/catalogue.json') if item['id']=='espgaluda2')
    certified={item['path']:item for item in receipt['outputs']}
    files=[]
    for name in game['runtimeFiles']:
        target=ROOT/name
        for destination in (target,target.with_suffix('.pdb')):
            source=output_root('host')/destination.name
            reject_link(source);reject_link(destination)
            expected=certified[destination.name]
            if digest(source)!=expected['sha256']:
                raise ValueError('Runtime source differs from certified host output')
            files.append((source,destination,expected))
    manifest=ROOT/'runtime/games/espgaluda2/runtime-manifest.json'
    reject_link(manifest)
    if manifest.exists():
        backup=ROOT/'.local/staging'/('espgaluda2-'+uuid.uuid4().hex+'-previous.json')
        backup.parent.mkdir(parents=True,exist_ok=True)
        manifest.rename(backup)
    outputs=[]
    for source,target,expected in files:
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
        if digest(target)!=expected['sha256'] or digest(source)!=expected['sha256']:
            raise ValueError('Staged runtime copy failed verification')
        outputs.append(dict(path=target.relative_to(ROOT).as_posix(),bytes=expected['bytes'],sha256=expected['sha256']))
    write_json_new(manifest,dict(format=1,game='espgaluda2',sourceCommit=receipt['sourceCommit'],
        sourceSnapshotSha256=receipt['sourceSnapshot']['sha256'],files=outputs,gameDataIncluded=False))
    print(json.dumps(dict(stagedFiles=len(outputs),gameDataIncluded=False),indent=2))


if __name__=='__main__':
    stage()
