"""Join the inspected ordinary/NEO ship response to actual original P1 delivery."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from daioujou_core_latency import analyze
from futari_pixel_response import locate
from latency_probe import read_trace
from project import digest,write_json_new
from vulkan_pixel_probe import read_capture,rgb


def review(neo):
    import numpy as np
    base=ROOT/'.local/input-latency-v080-r1'
    label='v082-daioujou-neo-core-pixel-r5' if neo else 'v082-daioujou-base-core-pixel-r6'
    launch_path,trace_path=[base/(label+s) for s in ('-launch.json','-trace.csv')]
    launch=json.loads(launch_path.read_text())
    trace=read_trace(trace_path.read_text())
    capture=read_capture(base/(label+'-pixels'),trace)
    if trace['pid']!=launch['pid'] or trace['overflow']:
        raise ValueError('Capture identity/completeness differs')
    fixture,=[Path(a.split('=',1)[1]) for a in launch['command'] if a.startswith('--collection_root=')]
    run,=[p for p in (fixture/'logs/daioujou').glob('run-*') if p.name.endswith('-'+str(launch['pid']))]
    config,stage=run/'config.snapshot.ini',fixture/'probe-stage.json'
    if digest(config)!=launch['config_sha256'] or digest(stage)!=launch['stage_sha256']:
        raise ValueError('Frozen configuration/binary manifest differs')
    configuration,=[json.loads(l) for l in (run/'stdout.jsonl').read_text().splitlines()
                    if '"event":"collection_configuration"' in l]
    if configuration['neo']!=neo or configuration['vsync']:
        raise ValueError('Unexpected runtime mode')
    expected=(1920,1080) if neo else (1280,720)
    if (capture['header']['width'],capture['header']['height'])!=expected:
        raise ValueError('Unexpected guest composition size')
    # The observer verifies LAST PadRead report, original callback6700 and P1
    # integration61250 delivery. A previous early API return is not substituted.
    core=analyze(trace,0x10000000,{0x27c04,0x1b55e})
    if core['paired_sources']!=12 or core['issues']:
        raise ValueError('Original input delivery evidence differs')
    sample,=[s for s in core['samples'] if s['command']==101]
    first=12 if neo else 11
    refs=(8,9,10,11) if neo else (8,9,10)
    boxes=((264,179,12,22),(255,218,30,18)) if neo else ((226,120,8,16),(220,148,20,12))
    images={i:np.asarray(rgb(capture,i)) for i in range(8,first+1)}
    templates=[[images[i][y:y+h,x:x+w] for i in refs] for x,y,w,h in boxes]
    tracks=[]
    for i,im in images.items():
        features=[max((locate(im,t,b[1],150,360) for t in group),key=lambda p:p['score'])
                  for b,group in zip(boxes,templates)]
        if any(p['score']<.85 or p['score']-p['distant_score']<.30 for p in features):
            raise ValueError('Ambiguous inspected nose/cockpit feature')
        tracks.append(dict(frame=i,features=features))
    expected_positions=([(264,255)]*4+[(255,246)] if neo else [(226,220)]*3+[(220,214)])
    if [tuple(p['x'] for p in t['features']) for t in tracks]!=expected_positions:
        raise ValueError('Stationary neighborhood/first left response changed')
    rows={r['seq']:r for r in trace['records']}
    returned,delivered,ended=[rows[sample['points'][k]] for k in ('pad_return','delivery_end','frame_end')]
    previous,response=capture['frames'][first-1],capture['frames'][first]
    prepare,present=rows[response['prepare']],rows[response['present_begin']]
    if not (rows[previous['present_begin']]['qpc']<returned['qpc']<=delivered['qpc']<=ended['qpc']<=prepare['qpc']<=present['qpc']):
        raise ValueError('Original input/callback/first-response ownership order differs')
    files=[launch_path,trace_path,config,stage,base/(label+'-pixels.csv'),base/(label+'-pixels.bin')]
    return dict(label=label,neo=neo,accepted_responses=1,command=101,first_response=first,
        original_delivery=sample,tracks=tracks,composition_size=expected,
        executable_sha256=launch['executable_sha256'],stage_sha256=launch['stage_sha256'],
        pad_return_to_response_present_ms=(present['qpc']-returned['qpc'])*1000/trace['frequency'],
        sources=[dict(path=str(p),sha256=digest(p)) for p in files])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    ordinary,neo=review(False),review(True)
    if any(ordinary[k]!=neo[k] for k in ('executable_sha256','stage_sha256')):
        raise ValueError('Pair uses different binaries')
    result=dict(ordinary=ordinary,neo=neo,reviewer_sha256=digest(Path(__file__)),
        scope='One inspected same-command response in each mode, same original ordinary quickslot01. Both respond in the current callback. Not a physical-latency distribution.')
    write_json_new(args.output,result)
    print(json.dumps({k:result[k]['pad_return_to_response_present_ms'] for k in ('ordinary','neo')}))


if __name__=='__main__':
    main()
