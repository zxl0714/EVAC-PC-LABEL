"""Launch only a pinned, isolated Xenia timing fixture and record its settings."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new


def region_text(values):
    x,y,width,height,capacity=values
    if (min(x,y)<0 or not 1<=width<=1024 or not 1<=height<=1024 or
        not 1<=capacity<=1024 or width*height*capacity>100000000):
        raise ValueError('ROI/capacity exceeds bounded pixel observer limits')
    return ','.join(map(str,values))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',required=True,choices=('futari','espgaluda2','sdoj','guwange'))
    parser.add_argument('--label',required=True,help='ASCII capture label, e.g. sdoj-r3')
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--region',type=int,nargs=5,metavar=('X','Y','W','H','CAPACITY'))
    parser.add_argument('--output-only',action='store_true',
                        help='Control run without original guest instruction observers')
    args=parser.parse_args()
    if not re.fullmatch(r'[a-z0-9-]{1,60}',args.label):
        raise ValueError('ASCII capture label required')
    base=ROOT/'.local/input-latency-v080-r1'
    fixture=args.fixture.resolve(strict=True)
    if not fixture.is_relative_to(base):
        raise ValueError('Private fixture required')
    stage=json.loads((fixture/'probe-stage.json').read_text())
    executable=Path(stage['executable'])
    if not executable.is_relative_to(base) or digest(executable)!=stage['exe_sha256']:
        raise ValueError('Pinned private executable identity changed')
    sites=fixture/'guest-sites.txt'
    if digest(sites)!=stage['guest_sites_sha256']:
        raise ValueError('Pinned guest observations changed')
    trace=base/f'xenia-{args.label}-trace.csv'
    log=base/f'xenia-{args.label}.log'
    receipt=base/f'xenia-{args.label}-launch.json'
    pixels=base/f'xenia-{args.label}-pixels'
    targets=[trace,log,receipt,Path(str(pixels)+'.csv'),Path(str(pixels)+'.bin')]
    if any(p.exists() for p in targets):
        raise ValueError('Capture output already exists; use a fresh label')
    env={k:v for k,v in os.environ.items() if not k.startswith('EVAC_LATENCY_')}
    settings=dict(EVAC_LATENCY_TRACE=str(trace),EVAC_LATENCY_CAPACITY='2000000',
                  EVAC_LATENCY_GUEST_SITES=str(sites),EVAC_LATENCY_TEST_PAD='xenia-'+args.label)
    if args.output_only:
        settings.pop('EVAC_LATENCY_GUEST_SITES')
    if args.region:
        settings.update(EVAC_LATENCY_PIXELS=str(pixels),EVAC_LATENCY_PIXEL_REGION=region_text(args.region))
    env.update(settings)
    config=fixture/'xenia-canary.config.toml'
    command=[str(executable),'--storage_root='+fixture.as_posix(),'--log_file='+log.as_posix(),
             '--discord=false',str(ROOT/'game-data'/args.game/'default.xex')]
    startup=subprocess.STARTUPINFO()
    startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow=1  # Interactive game window, not a background helper.
    process=subprocess.Popen(command,cwd=fixture,env=env,startupinfo=startup,
        stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    write_json_new(receipt,dict(pid=process.pid,command=command,environment=settings,
        executable_sha256=stage['exe_sha256'],config_sha256=digest(config),
        stage_sha256=digest(fixture/'probe-stage.json'),original_data_modified=False,
        guest_instruction_observers=not args.output_only))
    print(json.dumps(dict(pid=process.pid,pipe=settings['EVAC_LATENCY_TEST_PAD'],
                          trace=str(trace),receipt=str(receipt)),indent=2))


if __name__=='__main__':
    main()
