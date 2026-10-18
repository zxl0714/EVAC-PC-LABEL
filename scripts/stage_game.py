"""Stage verified player runtime files only; never copy game data or profiles."""
import argparse
import json
from pathlib import Path
import shutil
import uuid

from build_receipt import output_root, verify_receipt, verify_sdk, selected_games, DEFAULT_GAMES
from generate_catalogue import load
from project import ROOT, digest, reject_link, write_json_new


def stage_one(game,receipt,build):
    game_id=game['id']
    certified={item['path']:item for item in receipt['outputs']}
    files=[]
    certificate=f'runtime/games/{game_id}/runtime-manifest.ini'
    for name in game['runtimeFiles']:
        if name==certificate:
            continue
        target=ROOT/name
        for destination in (target,target.with_suffix('.pdb')):
            source=build/destination.name
            reject_link(source);reject_link(destination)
            expected=certified[destination.name]
            if digest(source)!=expected['sha256']:
                raise ValueError('Runtime source differs from certified host output')
            files.append((source,destination,expected))
    manifest=ROOT/f'runtime/games/{game_id}/runtime-manifest.json'
    reject_link(manifest)
    if manifest.exists():
        backup=ROOT/'.local/staging'/(game_id+'-'+uuid.uuid4().hex+'-previous.json')
        backup.parent.mkdir(parents=True,exist_ok=True)
        manifest.rename(backup)
    outputs=[]
    for source,target,expected in files:
        target.parent.mkdir(parents=True,exist_ok=True)
        # A matching shared SDK may be mapped by the other game. Leave its
        # verified bytes in place instead of rewriting an identical image.
        if not target.is_file() or digest(target)!=expected['sha256']:
            shutil.copy2(source,target)
        if digest(target)!=expected['sha256'] or digest(source)!=expected['sha256']:
            raise ValueError('Staged runtime copy failed verification')
        outputs.append(dict(path=target.relative_to(ROOT).as_posix(),bytes=expected['bytes'],sha256=expected['sha256']))
    write_json_new(manifest,dict(format=1,game=game_id,sourceCommit=receipt['sourceCommit'],
        sourceSnapshotSha256=receipt['sourceSnapshot']['sha256'],files=outputs,gameDataIncluded=False))
    # Player-safe certificate excludes PDB paths and machine-local directories.
    lines=['[runtime]','contract = 1','game = '+game_id,'source_commit = '+receipt['sourceCommit'],'','[files]']
    for item in outputs:
        if not item['path'].endswith('.pdb'):
            lines.append(item['path']+' = '+item['sha256'])
    certificate_path=ROOT/certificate
    reject_link(certificate_path)
    certificate_path.write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
    return dict(game=game_id,stagedFiles=len(outputs)+1,gameDataIncluded=False)


def stage(games=DEFAULT_GAMES):
    verify_sdk()
    receipt=verify_receipt('host',games)
    catalogue={game['id']:game for game in load(ROOT/'games/catalogue.json')}
    # A single successful build certifies the complete selected game set.
    for game in games:
        if not catalogue[game]['runtimeFiles']:
            raise ValueError('Runtime requirements are not declared: '+game)
    return [stage_one(catalogue[game],receipt,output_root('host',games)) for game in games]


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--games',type=selected_games,default=DEFAULT_GAMES)
    args=parser.parse_args()
    print(json.dumps(stage(args.games),indent=2))
