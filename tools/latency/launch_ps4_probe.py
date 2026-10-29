"""Launch a verified private native PS4 fixture with isolated test state."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest, write_json_new


def checked_pixel_region(text):
    if not re.fullmatch(r'\d+,\d+,\d+,\d+,\d+', text):
        raise ValueError('Five numeric pixel region fields required')
    x, y, width, height, capacity = map(int, text.split(','))
    if (max(x, y) > 2**32-1 or not 1 <= width <= 1024 or
            not 1 <= height <= 1024 or not 1 <= capacity <= 1024 or
            width*height*capacity > 100000000):
        raise ValueError('ROI/capacity exceeds bounded pixel observer limits')
    return text


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--game',choices=('ketsui','daioujou','esprade','feveron'),required=True)
    parser.add_argument('--label',required=True)
    parser.add_argument('--seed-profile',type=Path)
    parser.add_argument('--pixel-region',help='x,y,width,height,capacity; private bounded ROI')
    parser.add_argument('--timing',action='store_true',help='Enable existing worker/draw wall and CPU diagnostics')
    parser.add_argument('--esprade-test-ahead',type=int,choices=(0,1,2),
                        help='Private identity-pinned ESP Ra.De. core experiment only')
    parser.add_argument('--esprade-core-observer-off',action='store_true',
                        help='Keep only the shared input/presentation observers for CPU sampling')
    args=parser.parse_args()
    if args.pixel_region:
        checked_pixel_region(args.pixel_region)
    base=ROOT/'.local/input-latency-v080-r1'
    fixture=args.fixture.resolve(strict=True)
    if not fixture.is_relative_to(base) or not re.fullmatch('[a-z0-9-]{1,60}',args.label):
        raise ValueError('Private fixture and ASCII capture label required')
    stage_path=fixture/'probe-stage.json'
    stage=json.loads(stage_path.read_text())
    if stage['backend']!='evac':
        raise ValueError('Native PS4 fixture required')
    for item in stage['binaries']:
        if digest(fixture/item['path'])!=item['sha256']:
            raise ValueError('Staged measurement binary changed')
    data,=[Path(s['path']).parent for s in stage['game_executables'] if s['game']==args.game]
    for item in stage['game_executables']:
        if item['game']==args.game and digest(Path(item['path']))!=item['sha256']:
            raise ValueError('Original program identity changed')
    trace=base/(args.label+'-trace.csv')
    receipt=base/(args.label+'-launch.json')
    if trace.exists() or receipt.exists():
        raise ValueError('Fresh capture label required')
    profile=fixture/'user-data'/args.game/'default'
    seeded=[]
    if args.seed_profile:
        seed=args.seed_profile.resolve(strict=True)
        if not seed.is_relative_to(base) or profile.exists():
            raise ValueError('Stopped private profile and fresh destination required')
        shutil.copytree(seed,profile)
        for source in seed.rglob('*'):
            if source.is_file():
                relative=source.relative_to(seed)
                identity=digest(source)
                if digest(profile/relative)!=identity:
                    raise ValueError('Seed profile copy differs')
                seeded.append(dict(path=relative.as_posix(),sha256=identity))
    env={k:v for k,v in os.environ.items() if not k.startswith('EVAC_LATENCY_') and
         not k.startswith('CAVE_DIAGNOSTIC_') and k not in ('EVAC_PS4_TIMING_TRACE','EVAC_ESPRADE_TEST_AHEAD','EVAC_ESPRADE_TEST_NO_CORE')}
    settings=dict(EVAC_LATENCY_TRACE=str(trace),EVAC_LATENCY_CAPACITY='1048576',
                  EVAC_LATENCY_TEST_PAD=args.label)
    if args.timing:
        settings['EVAC_PS4_TIMING_TRACE']='1'
    if args.esprade_test_ahead is not None:
        if args.game!='esprade':raise ValueError('ESP Ra.De. core experiment requires esprade')
        settings['EVAC_ESPRADE_TEST_AHEAD']=str(args.esprade_test_ahead)
    if args.esprade_core_observer_off:
        if args.game!='esprade' or args.esprade_test_ahead is not None:
            raise ValueError('Core observer off requires ordinary ESP Ra.De. mode selection')
        settings['EVAC_ESPRADE_TEST_NO_CORE']='1'
    if args.pixel_region:
        pixel_prefix=base/(args.label+'-pixels')
        if any(Path(str(pixel_prefix)+suffix).exists() for suffix in ('.bin','.csv')):
            raise ValueError('Fresh pixel destination required')
        settings.update(EVAC_LATENCY_PIXELS=str(pixel_prefix),
                        EVAC_LATENCY_PIXEL_REGION=args.pixel_region)
    env.update(settings)
    executable=fixture/'runtime/ps4'/(args.game+'.exe')
    command=[str(executable),'--collection_root='+fixture.as_posix(),'--game_id='+args.game,
        '--launch_contract=1','--game_data_root='+data.as_posix(),'--user_data_root='+profile.as_posix()]
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=1
    process=subprocess.Popen(command,cwd=fixture,env=env,startupinfo=startup,
        stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    write_json_new(receipt,dict(pid=process.pid,command=command,environment=settings,
        executable_sha256=digest(executable),config_sha256=digest(fixture/'user-data/config.ini'),
        stage_sha256=digest(stage_path),seeded_test_profile=seeded,original_profile_modified=False))
    print(json.dumps(dict(pid=process.pid,pipe=args.label,trace=str(trace),receipt=str(receipt)),indent=2))


if __name__=='__main__':
    main()
