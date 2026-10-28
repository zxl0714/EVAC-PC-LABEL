"""Resolve original shad controller samples before using the shared core pairing."""
import argparse
import json
from pathlib import Path

from ketsui_core_latency import analyze
from latency_probe import read_trace
from project import digest,write_json_new
from ps4_latency import ps4_buttons


def attach_sources(trace):
    rows=[dict(r) for r in trace['records']];by_id={r['seq']:r for r in rows};queued={}
    for row in rows:
        if row['kind']==27:
            source=by_id.get(row['b'])
            if (not source or source['kind']!=14 or row['d']!=ps4_buttons(source['b']) or
                not source['qpc']<=row['qpc'] or not row['a'] or not row['c']):
                raise ValueError('Invalid original queued controller source')
            key=(row['a'],row['c'],row['d']);prior=queued.get(key)
            if prior and prior['b']!=row['b']: raise ValueError('Ambiguous queued source timestamp')
            queued[key]=row
    for row in rows:
        if row['kind']!=20: continue
        begin=by_id.get(row['a'])
        if not begin or begin['kind']!=18 or row['f']: raise ValueError('Unexpected shad report origin')
        sample=queued.get((begin['f'],row['d'],row['c']))
        if sample:
            if sample['qpc']>row['qpc']: raise ValueError('Queued source follows its report')
            row['f']=sample['b']
    return dict(trace,records=rows)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--launch',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    receipt=json.loads(args.launch.read_text())
    for item in receipt['audited_original_sources']:
        if digest(Path(item['path']))!=item['sha256']: raise ValueError('Original audited game changed')
    trace=read_trace(args.trace.read_text())
    if trace['pid']!=receipt['pid']: raise ValueError('Different launch process')
    bases={r['c'] for r in trace['records'] if r['kind']==10 and r['a']==98 and r['b']==0x29629}
    if len(bases)!=1: raise ValueError('Unique original main load address required')
    base,=bases;report=analyze(attach_sources(trace),main_base=base)
    report['scope']='Original shad sampled-controller queue -> LAST PadRead report -> original callback6610/input delivery; no image implied'
    report['main_image_base']=base
    report['sources']=[dict(path=str(p.resolve()),sha256=digest(p)) for p in (args.trace,args.launch,Path(__file__))]
    write_json_new(args.output,report)
    print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2))


if __name__=='__main__': main()
