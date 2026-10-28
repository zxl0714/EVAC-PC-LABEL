"""Pair Ketsui's original PadRead batch with its immediate core input delivery.

This stops at the core-frame callback, not a response image or Present. It
uses the exact source ID in the LAST returned report and the same-thread
original callback, with no intervening PadRead or earlier callback reuse.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys

from input_latency_report import distribution
from latency_probe import read_trace
from project import digest, write_json_new
from ps4_latency import ps4_buttons

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/latency'))
from inspect_ketsui_core_probe import inspect


def m2_buttons(value):
    if value not in (0x4008,0x4004):
        raise ValueError('Outside the verified right/left plus Square test mapping')
    return 0x101 if value&8 else 0x81


def analyze(trace,first=101,last=160,main_base=0x10000000):
    discovery=inspect(trace)
    rows=sorted(trace['records'],key=lambda r:(r['qpc'],r['seq']))
    by_id={r['seq']:r for r in rows}
    reports={};pending={};latest={};callback_polls={};ends={};applies={};apply_ends={}
    for r in rows:
        if r['kind']==18:
            pending[r['thread']]=r['seq'];latest.pop(r['thread'],None)
        elif r['kind']==20:
            reports.setdefault(r['a'],[]).append(r)
        elif r['kind']==19:
            began=by_id.get(r['a']);batch=reports.get(r['a'],[])
            if (not began or began['kind']!=18 or began['thread']!=r['thread'] or
                pending.pop(r['thread'],None)!=began['seq'] or began['e'] or
                r['b']!=r['c'] or [v['b'] for v in batch]!=list(range(r['c']))):
                raise ValueError('Invalid original PadRead batch identity/order')
            if batch:
                latest[r['thread']]=(began,r,batch[-1])
        elif r['kind']==10:
            if r['a']==100:
                poll=latest.pop(r['thread'],None)
                if poll:
                    callback_polls[r['seq']]=poll
            elif r['a']==101:
                ends[r['c']]=r
            elif r['a']==102 and r['d']==0:
                if r['c'] in applies:
                    raise ValueError('Multiple P1 deliveries within one original callback')
                applies[r['c']]=r
            elif r['a']==103 and r['e']==0:
                apply_ends[r['c']]=r
    candidates={};issues=Counter()
    for frame_id,poll in callback_polls.items():
        began,returned,report=poll
        source=by_id.get(report['f'])
        if not source or source['kind']!=14 or source['c']!=1 or not first<=source['a']<=last:
            continue
        # Reserve the first actual callback that receives this original batch
        # before validating its later fields. Do not replace a failed first
        # callback with a nicer subsequent one while the button remains held.
        if source['seq'] in candidates:
            continue
        candidates[source['seq']]=None
        frame=by_id[frame_id];delivery=applies.get(frame_id)
        if frame['f']!=main_base+0x29629:
            raise ValueError('Not the pinned original callback caller')
        ended=ends.get(frame_id);delivered=apply_ends.get(delivery['seq']) if delivery else None
        if not delivery or not delivered or not ended:
            issues['first_callback_has_no_complete_delivery']+=1;continue
        if (not report['e'] or report['c']!=ps4_buttons(source['b']) or
            delivery['e']>>32!=3 or delivery['e']&0xffffffff!=m2_buttons(source['b'])):
            issues['first_callback_buttons_or_type_do_not_match_source']+=1;continue
        if (delivered['d']!=frame_id or any(r['thread']!=frame['thread'] for r in
            (began,returned,report,delivery,delivered,ended)) or
            not source['qpc']<=returned['qpc']<=frame['qpc']<=delivery['qpc']<=delivered['qpc']<=ended['qpc']):
            raise ValueError('Original input/callback thread, ownership or clocks disagree')
        points=dict(source=source,pad_begin=began,pad_return=returned,last_report=report,
                    frame_begin=frame,delivery_begin=delivery,delivery_end=delivered,frame_end=ended)
        pairs=(('source','pad_return'),('pad_return','delivery_end'),('source','delivery_end'),
               ('frame_begin','delivery_end'),('delivery_end','frame_end'),('frame_begin','frame_end'))
        candidates[source['seq']]=dict(command=source['a'],source_buttons=source['b'],
            pad_buttons=report['c'],core_buttons=delivery['e']&0xffffffff,
            points={k:r['seq'] for k,r in points.items()},
            times={a+'_to_'+b+'_us':(points[b]['qpc']-points[a]['qpc'])*1e6/trace['frequency'] for a,b in pairs})
    sources=[r for r in rows if r['kind']==14 and r['c']==1 and first<=r['a']<=last]
    samples=[candidates[r['seq']] for r in sources if candidates.get(r['seq'])]
    return dict(capture=discovery['capture'],scope='Synthetic source -> last original PadRead report -> original callback6610 -> actual input delivery5c0a0 return -> callback completion; no response image',
        requested_sources=len(sources),paired_sources=len(samples),samples=samples,issues=dict(issues),
        unpaired_commands=[r['a'] for r in sources if not candidates.get(r['seq'])],
        statistics={k:distribution([s['times'][k] for s in samples],60) for k in (samples[0]['times'] if samples else [])},
        unmeasured=['actual_emulated_CPU_port_load','first_response_pixels','input_to_owned_Present',
                    'physical_frontend','same_response_display','panel_light','probe_off_control'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--image-log',type=Path,required=True)
    parser.add_argument('--audit',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    audit=json.loads(args.audit.read_text())
    if audit.get('callback_return_address')!=0x29629:
        raise ValueError('Pinned original caller audit required')
    expected,=[s['sha256'] for s in audit['sources'] if Path(s['path']).name=='eboot.bin']
    images=[]
    for line in args.image_log.read_text(encoding='utf-8').splitlines():
        if not line.startswith('{'):
            continue
        event=json.loads(line)
        if event.get('event')=='image_prepared' and event.get('image_name')=='eboot.bin':
            images.append(event)
    image,=images
    if image['source_sha256']!=expected:
        raise ValueError('Runtime main program differs from original caller audit')
    main_base=int(image['image_base'],16)
    report=analyze(read_trace(args.trace.read_text()),main_base=main_base)
    report['source']=dict(path=str(args.trace.resolve()),sha256=digest(args.trace))
    report['sources']=[dict(path=str(p.resolve()),sha256=digest(p)) for p in (args.trace,args.image_log,args.audit)]
    report['main_image_base']=main_base
    write_json_new(args.output,report)
    print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2))


if __name__=='__main__':
    main()
