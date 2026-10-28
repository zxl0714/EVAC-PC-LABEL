"""Launch the isolated native host probe; preserve exact binaries/config/env."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest, write_json_new
from launch_xenia_probe import region_text


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--label',required=True)
    parser.add_argument('--region',type=int,nargs=5)
    args=parser.parse_args()
    if not re.fullmatch(r'[a-z0-9-]{1,60}',args.label):
        raise ValueError('ASCII capture label required')
    base=ROOT/'.local/input-latency-v080-r1'
    fixture=args.fixture.resolve(strict=True)
    if not fixture.is_relative_to(base):
        raise ValueError('Private fixture required')
    stage_path=fixture/'probe-stage.json'
    stage=json.loads(stage_path.read_text())
    for item in stage['files']:
        if digest(fixture/item['path'])!=item['sha256']:
            raise ValueError('Staged binary identity changed')
    config=fixture/'user-data/config.ini'
    if digest(config)!=stage['config_sha256']:
        raise ValueError('Staged configuration changed')
    trace=base/f'evac-{args.label}-trace.csv'
    receipt=base/f'evac-{args.label}-launch.json'
    pixels=base/f'evac-{args.label}-pixels'
    if any(p.exists() for p in (trace,receipt,Path(str(pixels)+'.csv'),Path(str(pixels)+'.bin'))):
        raise ValueError('Fresh capture label required')
    env={k:v for k,v in os.environ.items() if not k.startswith('EVAC_LATENCY_')}
    settings=dict(EVAC_LATENCY_TRACE=str(trace),EVAC_LATENCY_CAPACITY='1000000',
                  EVAC_LATENCY_TEST_PAD='evac-'+args.label)
    if args.region:
        settings.update(EVAC_LATENCY_PIXELS=str(pixels),EVAC_LATENCY_PIXEL_REGION=region_text(args.region))
    env.update(settings)
    executable=fixture/'runtime/xbox360'/(stage['game']+'.exe')
    command=[str(executable),'--collection_root='+fixture.as_posix(),'--game_id='+stage['game'],
        '--launch_contract=1','--output_latency_trace=true','--log_max_file_size_mb=128']
    if stage['game']=='sdoj':
        command+=['--sdoj_input_trace=true','--sdoj_latency_trace=true','--sdoj_frame_delay_trace=true']
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=1
    process=subprocess.Popen(command,cwd=fixture,env=env,startupinfo=startup,
        stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    write_json_new(receipt,dict(pid=process.pid,command=command,environment=settings,
        executable_sha256=digest(executable),config_sha256=digest(config),
        stage_sha256=digest(stage_path),production_sources_modified=False))
    print(json.dumps(dict(pid=process.pid,pipe=settings['EVAC_LATENCY_TEST_PAD'],
                          trace=str(trace),receipt=str(receipt)),indent=2))


if __name__=='__main__':
    main()
