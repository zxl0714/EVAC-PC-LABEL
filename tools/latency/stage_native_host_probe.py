"""Assemble a private host-only latency fixture from an existing test collection."""
import argparse
import json
from pathlib import Path
import shutil
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from generate_catalogue import load
from project import digest, write_json_new


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',choices=('sdoj','guwange'),required=True)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--destination',type=Path,required=True)
    parser.add_argument('--frame-delay',type=int,choices=(1,2,3),default=1)
    args=parser.parse_args()
    build,baseline,destination=(p.resolve() for p in (args.build,args.baseline,args.destination))
    private=ROOT/'.local/input-latency-v080-r1'
    if (not all(p.is_relative_to(private) for p in (build,destination)) or
        not baseline.is_relative_to(ROOT/'.local') or destination.exists()):
        raise ValueError('Fresh private fixture and private build/baseline required')
    receipt=json.loads((build/'build-result.json').read_text())
    executable=build/(args.game+'.exe')
    if not receipt['original_inputs_unchanged'] or digest(executable)!=receipt['files'][executable.name]:
        raise ValueError('Completed host probe build required')
    runtime=private/'evac-futari-pixels-build-r2/rexruntime.dll'
    runtime_receipt=json.loads((runtime.parent/'build-result.json').read_text())
    if digest(runtime)!=runtime_receipt['files']['rexruntime.dll']:
        raise ValueError('Pinned private pixel runtime changed')
    game=next(g for g in load(ROOT/'games/catalogue.json') if g['id']==args.game)
    overrides={executable.name:executable,'rexruntime.dll':runtime}
    if args.game=='guwange':
        module=build/'guwange_Guwange01_x360.dll'
        if digest(module)!=receipt['files'][module.name]:
            raise ValueError('Private Guwange module identity changed')
        overrides[module.name]=module
    destination.mkdir(parents=True)
    copied=[]
    for name in game['runtimeFiles']:
        if name.endswith('runtime-manifest.ini'):
            continue
        source=overrides.get(Path(name).name,baseline/name)
        target=destination/name
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
        if digest(source)!=digest(target):
            raise ValueError('Fixture copy identity differs')
        copied.append(dict(source=str(source),path=name,sha256=digest(target)))
        symbols=source.with_suffix('.pdb')
        if symbols.is_file() and source!=runtime:
            shutil.copy2(symbols,target.with_suffix('.pdb'))
    profile=baseline/'user-data'/args.game
    shutil.copytree(profile,destination/'user-data'/args.game)
    profiles={p.relative_to(profile).as_posix():digest(p) for p in profile.rglob('*') if p.is_file()}
    for name,expected in profiles.items():
        if digest(destination/'user-data'/args.game/name)!=expected:
            raise ValueError('Test profile copy differs')
    config=destination/'user-data/config.ini'
    mode=f'frame_delay = {args.frame_delay}' if args.game=='sdoj' else ''
    # Guwange already uses I/J/K/L for its original right-stick actions.
    # Its synthetic-pad probe needs no keyboard remapping; preserve defaults.
    directions=('I','K','J','L') if args.game=='sdoj' else ('Up','Down','Left','Right')
    config.write_text(f'''[collection]
language = en
selected_game = {args.game}
[display]
fullscreen = false
vsync = true
[audio]
volume = 0.25
[community]
enabled = false
nickname = OfflineLatency
[gamepad]
enabled = false
[keyboard]
up = {directions[0]}
down = {directions[1]}
left = {directions[2]}
right = {directions[3]}
[game.{args.game}]
data_dir = E:/Projects/EVAC-PC-LABEL/game-data/{args.game}
{mode}
''',encoding='ascii')
    write_json_new(destination/'probe-stage.json',dict(game=args.game,files=copied,
        baseline_test_profile=str(profile),profile_files=profiles,
        frame_delay=args.frame_delay if args.game=='sdoj' else None,config_sha256=digest(config),
        build_receipt_sha256=digest(build/'build-result.json'),
        original_profile_modified=False,original_game_modules_recompiled=args.game=='guwange',release_certified=False))
    print(destination)


if __name__=='__main__':
    main()
