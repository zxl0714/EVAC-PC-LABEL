"""Inspect core observations without promoting paused/menu inputs to latency."""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from latency_probe import read_trace
from project import digest, write_json_new


def inspect(trace):
    if not trace['complete']:
        raise ValueError('Complete core-discovery trace required')
    rows=trace['records'];by_id={r['seq']:r for r in rows}
    observations=[r for r in rows if r['kind']==10]
    pcs={98:0x29629,99:0x50088,100:0x6610,101:0x6610,102:0x5c0a0,103:0x5c0a0,104:0x5c0a0}
    if any(pcs.get(r['a'])!=r['b'] for r in observations):
        raise ValueError('Core probe is not the pinned original site set')
    frames={};seen=set()
    for end in observations:
        if end['a']!=101:
            continue
        began=by_id.get(end['c'])
        if not began or began['kind']!=10 or began['a']!=100 or began['thread']!=end['thread'] or began['qpc']>end['qpc'] or began['seq'] in seen:
            raise ValueError('Invalid core callback completion identity')
        seen.add(began['seq']);frames[began['seq']]=(began,end)
    sources=[r for r in rows if r['kind']==14 and r['c']==1 and 101<=r['a']<=160]
    begins=[r for r in observations if r['a']==100]
    interval=(min(r['qpc'] for r in begins),max(e['qpc'] for _,e in frames.values())) if frames else None
    outside=[r['a'] for r in sources if not interval or not interval[0]<=r['qpc']<=interval[1]]
    deliveries=[r for r in observations if r['a']==102]
    joined=[]
    for r in deliveries:
        frame=frames.get(r['c'])
        if not frame:
            continue
        if not frame[0]['qpc']<=r['qpc']<=frame[1]['qpc'] or r['thread']!=frame[0]['thread']:
            raise ValueError('Input delivery is outside its original callback')
        joined.append(r)
    return dict(capture={k:trace[k] for k in ('pid','complete','committed','overflow','incomplete')},
        original_callback_frames=len(frames),unfinished_callbacks=len(begins)-len(frames),
        core_callback_interval_qpc=interval,requested_sources=len(sources),
        sources_outside_core_interval=outside,input_delivery_calls=len(deliveries),
        deliveries_in_owned_callback=len(joined),
        player_type_buttons=[dict(player=p,type=t,buttons=hex(b),count=n) for (p,t,b),n in
            Counter((r['d'],r['e']>>32,r['e']&0xffffffff) for r in deliveries).items()],
        original_callers=[dict(address=hex(pc),count=n) for pc,n in Counter(r['f'] for r in begins).items()],
        diagnostic_output_words=[dict(words=hex(v),count=n) for v,n in
            Counter(e['f'] for _,e in frames.values()).items()],
        scope='Core-callback/input-delivery discovery only, not input-to-first-image latency',
        unmeasured=['actual_emulated_CPU_port_load','first_response_pixels','input_to_owned_Present',
                    'physical_frontend','same_response_display','panel_light'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    report=inspect(read_trace(args.trace.read_text()))
    report['source']=dict(path=str(args.trace.resolve()),sha256=digest(args.trace))
    write_json_new(args.output,report)
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
