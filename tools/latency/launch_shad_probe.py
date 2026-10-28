"""Launch an isolated shad observer with original-file identity checks."""
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
from project import digest,write_json_new


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--label',required=True)
    parser.add_argument('--seed-user',type=Path)
    parser.add_argument('--pixel-region')
    parser.add_argument('--audit',type=Path,help='Original per-game audit; default is the accepted Ketsui audit')
    args=parser.parse_args()
    base=ROOT/'.local/input-latency-v080-r1';fixture=args.fixture.resolve(strict=True)
    if not fixture.is_relative_to(base) or not re.fullmatch('[a-z0-9-]{1,60}',args.label):
        raise ValueError('Private fixture and fresh ASCII label required')
    stage_path=fixture/'probe-stage.json';stage=json.loads(stage_path.read_text())
    if stage['backend']!='shad': raise ValueError('shad fixture required')
    for item in stage['binaries']:
        if digest(fixture/item['path'])!=item['sha256']: raise ValueError('Observer binary identity differs')
    audit_path=args.audit.resolve(strict=True) if args.audit else base/'ketsui-core-flow-r2/report.json'
    if not audit_path.is_relative_to(base):raise ValueError('Private original-game audit required')
    audit=json.loads(audit_path.read_text())
    for item in audit['sources']:
        if digest(Path(item['path']))!=item['sha256']: raise ValueError('Original audited game identity differs')
    original,=[Path(item['path']) for item in audit['sources'] if Path(item['path']).name=='eboot.bin']
    staged,=[item for item in stage['game_executables'] if item['game']==audit['game']]
    if Path(staged['path']).resolve()!=original.resolve() or staged['sha256']!=digest(original):
        raise ValueError('Selected original game differs from staged fixture')
    trace,receipt,log=(base/(args.label+suffix) for suffix in ('-trace.csv','-launch.json','.log'))
    if any(p.exists() for p in (trace,receipt,log)): raise ValueError('Fresh capture label required')
    seeds=[]
    if args.seed_user:
        seed=args.seed_user.resolve(strict=True)
        if not seed.is_relative_to(base): raise ValueError('Stopped private test profile required')
        for name in ('home','users.json'):
            source,target=seed/name,fixture/'user'/name
            if target.exists(): raise ValueError('Fresh profile destination required')
            if source.is_dir(): shutil.copytree(source,target)
            else: shutil.copy2(source,target)
            for file in source.rglob('*') if source.is_dir() else [source]:
                if not file.is_file(): continue
                relative=file.relative_to(seed);identity=digest(file)
                if digest(fixture/'user'/relative)!=identity: raise ValueError('Test profile copy differs')
                seeds.append(dict(path=relative.as_posix(),sha256=identity))
    settings=dict(EVAC_LATENCY_TRACE=str(trace),EVAC_LATENCY_CAPACITY='2097152',EVAC_LATENCY_TEST_PAD=args.label)
    if args.pixel_region:
        if not re.fullmatch(r'\d+,\d+,\d+,\d+,\d+',args.pixel_region): raise ValueError('Five numeric ROI fields required')
        prefix=base/(args.label+'-pixels')
        if any(Path(str(prefix)+suffix).exists() for suffix in ('.bin','.csv')): raise ValueError('Fresh ROI output required')
        settings.update(EVAC_LATENCY_PIXELS=str(prefix),EVAC_LATENCY_PIXEL_REGION=args.pixel_region)
    env={k:v for k,v in os.environ.items() if not k.startswith('EVAC_LATENCY_') and not k.startswith('CAVE_DIAGNOSTIC_')}
    env.update(settings)
    executable=fixture/'shadps4.exe';command=[str(executable),str(original)]
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=1
    with log.open('xb') as output:
        process=subprocess.Popen(command,cwd=fixture,env=env,startupinfo=startup,
            stdin=subprocess.DEVNULL,stdout=output,stderr=subprocess.STDOUT)
    write_json_new(receipt,dict(pid=process.pid,command=command,environment=settings,
        executable_sha256=digest(executable),config_sha256=digest(fixture/'user/config.json'),
        stage_sha256=digest(stage_path),audited_original_sources=audit['sources'],
        original_audit_sha256=digest(audit_path),seeded_private_profile=seeds,original_profile_modified=False))
    print(json.dumps(dict(pid=process.pid,pipe=args.label,trace=str(trace),log=str(log)),indent=2))


if __name__=='__main__': main()
