"""Create isolated G2 Xenia settings using the already-pinned observer build."""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--destination',type=Path,required=True)
    a=p.parse_args()
    base=ROOT/'.local/input-latency-v080-r1'
    destination=a.destination.resolve()
    if not destination.is_relative_to(base):
        raise ValueError('Private stage required')
    original=base/'xenia-futari'
    binary=base/'xenia-futari-pixels-build-r1/xenia_canary.exe'
    receipt=json.loads((base/'xenia-futari-pixels-build-r1/build-result.json').read_text())
    if digest(binary)!=receipt['exe_sha256']:
        raise ValueError('Pinned observation EXE identity changed')
    prep=base/'evac-g2-probe-r2'
    pinned=json.loads((prep/'preparation.json').read_text())
    for site in pinned['sites']:
        if digest(ROOT/'game-data/espgaluda2'/(site['module']+'.xex'))!=site['input_sha256']:
            raise ValueError('Original game module changed')
    destination.mkdir(exist_ok=False)
    text=(original/'xenia-canary.config.toml').read_text(encoding='utf-8')
    text,count=re.subn(r'^storage_root\s*=\s*"[^"]*"',
                      'storage_root = '+json.dumps(destination.as_posix()),text,flags=re.M)
    if count!=1:
        raise ValueError('Storage root configuration anchor changed')
    (destination/'xenia-canary.config.toml').write_text(text,encoding='utf-8')
    # Only the existing stopped TEST account. No Futari save/replay is copied.
    for relative in ('xconfig.settings','content/E03000002770A370/FFFE07D1'):
        source=original/relative
        output=destination/relative
        output.parent.mkdir(parents=True,exist_ok=True)
        if source.is_dir():
            shutil.copytree(source,output)
        else:
            shutil.copy2(source,output)
    shutil.copy2(prep/'guest-sites.txt',destination/'guest-sites.txt')
    files={str(f.relative_to(destination)):digest(f) for f in destination.rglob('*') if f.is_file()}
    write_json_new(destination/'probe-stage.json',dict(executable=str(binary),exe_sha256=digest(binary),
        baseline_config_sha256=digest(original/'xenia-canary.config.toml'),files=files,
        game_programs={s['module']:s['input_sha256'] for s in pinned['sites']},
        original_profile_modified=False,original_game_modified=False,
        native_frame_delay_patch_applied=False,first_scene='Xbox360 / Play / Stage1 / Ageha'))
    print(destination)


if __name__=='__main__':
    main()
