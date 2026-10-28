"""Pair original Daioujou PadRead reports and callback delivery, including tail dispatch.

Raw masks identify the audited digital report before the integration's own bit
transform. They are not claimed to be emulated CPU port reads or visible response.
"""
import argparse
from collections import Counter
import json
from pathlib import Path

from input_latency_report import distribution
from latency_probe import read_trace
from project import digest,write_json_new
from ps4_latency import ps4_buttons

SITES={99:0x400088,100:0x6700,101:0x6700,102:0x61250,103:0x61250,104:0x61250}


def analyze(trace,main_base,allowed_returns,first=101,last=112):
    if not trace['complete']:
        raise ValueError('Complete Daioujou trace required')
    if not allowed_returns or not set(allowed_returns)<={0x27c04,0x1b55e}:
        raise ValueError('Audited direct/parent return addresses required')
    rows=sorted(trace['records'],key=lambda r:(r['qpc'],r['seq']))
    by_id={r['seq']:r for r in rows}
    observations=[r for r in rows if r['kind']==10]
    if any(SITES.get(r['a'])!=r['b'] for r in observations):
        raise ValueError('Daioujou original observation sites differ')
    frames={};ends={};applies={};apply_ends={};polls={};reports={};latest={};pending={}
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
                raise ValueError('Original PadRead batch identity/order differs')
            if batch: latest[r['thread']]=(began,r,batch[-1])
        elif r['kind']==10:
            if r['a']==100:
                frames[r['seq']]=r
                poll=latest.pop(r['thread'],None)
                if poll: polls[r['seq']]=poll
            elif r['a']==101:
                if r['c'] in ends: raise ValueError('Duplicate callback completion')
                ends[r['c']]=r
            elif r['a']==102 and r['d']==0:
                if r['c'] in applies: raise ValueError('Duplicate P1 delivery')
                applies[r['c']]=r
            elif r['a']==103 and r['e']==0:
                if r['c'] in apply_ends: raise ValueError('Duplicate P1 delivery completion')
                apply_ends[r['c']]=r
    candidates={};issues=Counter()
    for frame_id,poll in polls.items():
        began,returned,report=poll;source=by_id.get(report['f'])
        if not source or source['kind']!=14 or source['c']!=1 or not first<=source['a']<=last:
            continue
        if source['seq'] in candidates: continue
        candidates[source['seq']]=None
        frame=frames[frame_id];delivery=applies.get(frame_id);ended=ends.get(frame_id)
        delivered=apply_ends.get(delivery['seq']) if delivery else None
        if frame['f']-main_base not in allowed_returns:
            raise ValueError('Callback is not the original direct or tail-parent caller')
        if not delivery or not delivered or not ended:
            issues['first_callback_incomplete']+=1;continue
        if source['b'] not in (0x4008,0x4004): raise ValueError('Outside audited direction/Square pulses')
        expected=0x101 if source['b']==0x4008 else 0x81
        if not report['e'] or report['c']!=ps4_buttons(source['b']) or delivery['e']!=(3<<32)|expected:
            issues['first_callback_raw_report_mismatch']+=1;continue
        if (delivered['d']!=frame_id or any(r['thread']!=frame['thread'] for r in
            (began,returned,report,delivery,delivered,ended)) or
            not source['qpc']<=returned['qpc']<=frame['qpc']<=delivery['qpc']<=delivered['qpc']<=ended['qpc']):
            raise ValueError('Input/callback ownership, thread or clocks differ')
        points=dict(source=source,pad_begin=began,pad_return=returned,last_report=report,
                    frame_begin=frame,delivery_begin=delivery,delivery_end=delivered,frame_end=ended)
        pairs=(('source','pad_return'),('pad_return','delivery_end'),('source','delivery_end'),
               ('frame_begin','delivery_end'),('delivery_end','frame_end'),('frame_begin','frame_end'))
        candidates[source['seq']]=dict(command=source['a'],source_buttons=source['b'],
            pad_buttons=report['c'],raw_digital_buttons=expected,caller_offset=frame['f']-main_base,
            points={k:r['seq'] for k,r in points.items()},
            times={a+'_to_'+b+'_us':(points[b]['qpc']-points[a]['qpc'])*1e6/trace['frequency'] for a,b in pairs})
    sources=[r for r in rows if r['kind']==14 and r['c']==1 and first<=r['a']<=last]
    samples=[candidates[r['seq']] for r in sources if candidates.get(r['seq'])]
    return dict(game='daioujou',capture={k:trace[k] for k in ('pid','complete','committed','overflow','incomplete')},
        requested_sources=len(sources),paired_sources=len(samples),samples=samples,issues=dict(issues),
        original_callback_count=len(frames),
        original_callers=[dict(offset=hex(pc-main_base),count=n) for pc,n in Counter(r['f'] for r in frames.values()).items()],
        unpaired_commands=[r['a'] for r in sources if not candidates.get(r['seq'])],
        statistics={k:distribution([s['times'][k] for s in samples],60) for k in (samples[0]['times'] if samples else [])},
        scope='Synthetic source -> LAST original PadRead report -> first same-thread callback6700 -> original integration61250 return',
        raw_layout='Digital type3 low16 at+8; observed raw101/right+Square and81/left+Square BEFORE original high-bit transform',
        unmeasured=['actual_emulated_CPU_port_load','first_response_pixels','physical_frontend','OS_display','panel_light'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--image-log',type=Path,required=True)
    parser.add_argument('--audit',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();audit=json.loads(args.audit.read_text())
    if (audit['game'],audit['frame_callback'],audit['tail_callback_return'])!=('daioujou',0x6700,0x1b55e):
        raise ValueError('Original module and both caller paths must be audited')
    hashes={Path(s['path']).name:s['sha256'] for s in audit['sources']}
    for item in audit['sources']:
        if digest(Path(item['path']))!=item['sha256']: raise ValueError('Original game changed')
    images=[json.loads(line) for line in args.image_log.read_text(encoding='utf-8').splitlines() if line.startswith('{')]
    prepared=[r for r in images if r.get('event')=='image_prepared']
    for name,expected in hashes.items():
        image,=[r for r in prepared if r.get('image_name')==name]
        if image['source_sha256']!=expected: raise ValueError('Loaded module differs from original audit')
    main_image,=[r for r in prepared if r.get('image_name')=='eboot.bin']
    main_base=int(main_image['image_base'],16)
    result=analyze(read_trace(args.trace.read_text()),main_base,
                   {audit['direct_callback_return'],audit['tail_callback_return']})
    result['main_image_base']=main_base
    result['sources']=[dict(path=str(p.resolve()),sha256=digest(p)) for p in
        (Path(__file__),args.trace,args.image_log,args.audit)]
    write_json_new(args.output,result)
    print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2))


if __name__=='__main__': main()
