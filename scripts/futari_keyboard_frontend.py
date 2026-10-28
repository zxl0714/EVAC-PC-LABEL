"""Pair original keyboard diagnostics with inspected Futari movement images."""
import argparse
from bisect import bisect_left
import json
from pathlib import Path
import re

from futari_pixel_response import locate
from input_latency_report import distribution
from latency_probe import read_trace
from pixel_probe import read_capture, rgb
from project import digest, write_json_new

KEY = re.compile(r'INPUT_DIAG key=(\d+) down=(true|false) repeat=(true|false) before=([0-9A-F]+) after=([0-9A-F]+) polls=(\d+) t_us=(\d+) frame=(\d+)')
POLL = re.compile(r'\[t(\d+)\] INPUT_DIAG poll count=(\d+) active=true buttons=([0-9A-F]+) t_us=(\d+) frame=(\d+)')


def frontend(text):
    keys, polls = [], []
    for line in text.splitlines():
        if m := KEY.search(line):
            key, down, repeat, before, after, count, time, frame = m.groups()
            keys.append(dict(key=int(key), down=down=='true', repeat=repeat=='true',
                before=int(before,16), after=int(after,16), polls=int(count), us=int(time), frame=int(frame)))
        if m := POLL.search(line):
            thread, count, buttons, time, frame = map(str,m.groups())
            polls.append(dict(thread=int(thread), count=int(count), buttons=int(buttons,16), us=int(time), frame=int(frame)))
    downs = [k for k in keys if k['down'] and not k['repeat'] and k['before']==0 and k['after'] in (4,8)]
    pairs = []
    for index,key in enumerate(downs):
        deadline = downs[index+1]['us'] if index+1<len(downs) else key['us']+500000
        candidates = [p for p in polls if p['count']==key['polls']+1 and p['buttons']==key['after'] and key['us']<=p['us']<deadline]
        releases = [r for r in keys if r['key']==key['key'] and not r['down'] and key['us']<=r['us']<deadline]
        pairs.append(dict(key=key, poll=candidates[0] if len(candidates)==1 else None,
            hold_us=releases[0]['us']-key['us'] if releases else None,
            next_key_us=deadline))
    return pairs


def main():
    import numpy as np
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log',type=Path,required=True)
    parser.add_argument('--trace',type=Path,required=True)
    parser.add_argument('--pixels',type=Path,required=True)
    parser.add_argument('--clock',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    trace=read_trace(args.trace.read_text())
    capture=read_capture(args.pixels)
    clock=json.loads(args.clock.read_text())
    if not trace['complete'] or capture['footer']['overflow'] or trace['pid']!=capture['header']['pid']:
        raise ValueError('Incomplete or different-process capture')
    if (not clock.get('valid') or clock['frequency']!=trace['frequency'] or clock['failures'] or
            not clock.get('samples') or clock.get('mapping')!='floor(qpc*1000000/frequency)'):
        raise ValueError('Original clock check required')
    pairs=frontend(args.log.read_text(encoding='utf8'))
    # Inspected character body in the Arcade ROI, excluding the bright shield.
    template=np.asarray(rgb(capture,30))[105:170,135:185]
    tracks=[locate(np.asarray(rgb(capture,i)),template,105,50,240)
            for i in range(len(capture['frames']))]
    times=[f['present_begin']*1e6/trace['frequency'] for f in capture['frames']]
    accepted, excluded=[], []
    for number,pair in enumerate(pairs,1):
        key,poll=pair['key'],pair['poll']
        if poll is None:
            excluded.append(dict(number=number,reason='no_exact_next_keyboard_poll'));continue
        pivot=bisect_left(times,key['us'])
        end=bisect_left(times,min(pair['next_key_us'],key['us']+100000))
        if pivot<3 or end<=pivot:
            excluded.append(dict(number=number,reason='outside_pixel_interval'));continue
        indexes=list(range(pivot-3,min(end+2,len(tracks))))
        good=lambda p: p['score']>=.8 and p['score']-p['distant_score']>=.025
        baseline=[tracks[i]['x'] for i in range(pivot-3,pivot)]
        if any(not good(tracks[i]) for i in range(pivot-3,pivot)) or max(baseline)-min(baseline)>1:
            excluded.append(dict(number=number,reason='uncertain_or_moving_baseline'));continue
        direction=1 if key['after']==8 else -1
        first=next((i for i in range(pivot,end) if (tracks[i]['x']-baseline[-1])*direction>=2),None)
        if first is None or first+1>=len(tracks) or any(not good(tracks[i]) for i in range(pivot,first+2)):
            excluded.append(dict(number=number,reason='uncertain_response_image'));continue
        if times[first]<poll['us']:
            excluded.append(dict(number=number,reason='image_precedes_observed_keyboard_poll'));continue
        shift=(tracks[first]['x']-baseline[-1])*direction
        if not 4<=shift<=12 or abs(tracks[first+1]['x']-tracks[first]['x'])>2:
            excluded.append(dict(number=number,reason='not_one_directional_step'));continue
        accepted.append(dict(number=number,**pair,first_response_index=first,
            before_indexes=[pivot-3,pivot-2,pivot-1], before_x=baseline,
            response_x=[tracks[first]['x'],tracks[first+1]['x']],
            key_to_poll_us=poll['us']-key['us'],
            key_to_response_present_us=times[first]-key['us']))
    result=dict(scope='Actual window key handler -> original keyboard snapshot; visually verified same-image Present',
        setting='Ver1.5 Arcade Training Stage1 Original RecoNormal 1V; diagnostic Numpad4/6 bindings',
        pid=trace['pid'], pairs=pairs, accepted=accepted,excluded=excluded, tracks=tracks,
        frontend_statistics=distribution([p['poll']['us']-p['key']['us'] for p in pairs if p['poll']],60),
        response_statistics=distribution([s['key_to_response_present_us'] for s in accepted],60),
        unmeasured=['device_USB_OS_dispatch_before_key_handler','SDL_gamepad','scanout_photons','probe_off_control'],
        sources=[dict(path=str(p.resolve()),sha256=digest(p)) for p in (args.log,args.trace,args.clock,
            Path(str(args.pixels)+'.csv'),Path(str(args.pixels)+'.bin'))])
    write_json_new(args.output,result)
    print(json.dumps({k:result[k] for k in ('frontend_statistics','response_statistics')},indent=2))
    print('presses',len(pairs),'matched original poll',sum(bool(p['poll']) for p in pairs),'pixel responses',len(accepted))


if __name__=='__main__':main()
