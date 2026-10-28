"""Revalidate one explicitly inspected native Ketsui right-movement response.

The first responsive snapshot and first responsive submitted image can differ.
Blink/shield phases use separate stationary templates; only the stable body
feature is accepted on every frame. This is not a physical input measurement.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from futari_pixel_response import locate
from ketsui_core_latency import analyze
from latency_probe import read_trace
from project import digest,write_json_new
from vulkan_pixel_probe import read_capture,rgb


def reviewed_response(trace,capture,sample,primary,secondary):
    indexes=list(range(12,22))
    if set(primary)!=set(indexes) or set(secondary)!={12,14,16,18,20}:
        raise ValueError('The explicitly inspected neighborhood differs')
    def confident(point):
        return point['score']>=.8 and point['score']-point['distant_score']>=.025
    if not all(confident(v) for v in (*primary.values(),*secondary.values())):
        raise ValueError('Uncertain body feature match')
    baseline=[primary[i]['x'] for i in range(12,18)]
    check_baseline=[secondary[i]['x'] for i in (12,14,16)]
    if max(baseline)-min(baseline)>1 or max(check_baseline)-min(check_baseline)>1:
        raise ValueError('Body already moving before the tested input')
    first=next((i for i in range(18,22) if primary[i]['x']-baseline[-1]>=2),None)
    if first!=18 or any(not 2<=primary[i+1]['x']-primary[i]['x']<=12 for i in range(first,21)):
        raise ValueError('Inspected first right step or continuation changed')
    if (secondary[18]['x']-check_baseline[-1]!=primary[18]['x']-baseline[-1] or
        secondary[20]['x']-check_baseline[-1]!=primary[20]['x']-baseline[-1]):
        raise ValueError('Independent body features disagree on right displacement')
    if sample['command']!=101 or sample['source_buttons']!=0x4008 or sample['core_buttons']!=0x101:
        raise ValueError('Not the inspected first right plus Square input')
    by_id={r['seq']:r for r in trace['records']}
    source=by_id[sample['points']['source']]
    delivery=by_id[sample['points']['delivery_end']]
    frames=capture['frames'];first_frame=frames[first]
    # read_capture checks snapshot -> ready -> Present identity. Here only
    # submitted, successful images with the inspected right response qualify.
    submitted=[f for f in frames[first:22] if f['present_begin'] and f['result']==0]
    if not submitted:
        raise ValueError('No inspected response was submitted')
    shown=submitted[0]
    if first_frame['present_begin'] or shown['index']!=19:
        raise ValueError('Inspected first response replacement/submission changed')
    first_ready=by_id[first_frame['ready']]
    shown_ready=by_id[shown['ready']]
    present=by_id[shown['present_begin']]
    source_release,=[r for r in trace['records'] if r['kind']==14 and r['a']==101 and r['c']==0]
    if not source['qpc']<=delivery['qpc']<=first_ready['qpc']<=shown_ready['qpc']<=present['qpc']:
        raise ValueError('Input/response clock order differs')
    if by_id[first_frame['prepare']]['qpc']<=delivery['qpc'] or by_id[frames[17]['prepare']]['qpc']>=source['qpc']:
        raise ValueError('Reviewed first response no longer brackets the input')
    def ms(a,b): return (b['qpc']-a['qpc'])*1000/trace['frequency']
    result=dict(method='Explicit visual inspection plus two stationary-phase body features; not all pulses accepted',
        accepted_responses=1,command=101,baseline_frames=list(range(12,18)),
        first_response_snapshot=first,first_response_snapshot_presented=bool(first_frame['present_begin']),
        first_submitted_response_snapshot=shown['index'],response_present_seq=present['seq'],
        source_seq=source['seq'],delivery_end_seq=delivery['seq'],
        first_response_ready_seq=first_ready['seq'],submitted_response_ready_seq=shown_ready['seq'],
        primary_tracks=primary,secondary_even_phase_tracks=secondary,
        synthetic_source_to_core_delivery_ms=ms(source,delivery),
        core_delivery_to_first_response_handoff_ms=ms(delivery,first_ready),
        first_response_to_submitted_response_handoff_ms=ms(first_ready,shown_ready),
        submitted_response_handoff_to_present_ms=ms(shown_ready,present),
        core_delivery_to_response_present_ms=ms(delivery,present),
        pad_return_to_response_present_ms=ms(by_id[sample['points']['pad_return']],present),
        synthetic_source_to_response_present_ms=ms(source,present),
        first_response_handoff_before_source_release=first_ready['qpc']<source_release['qpc'],
        p95_ms=None,scope='Synthetic published state / PadRead return / original input delivery -> first visibly right-moving image submitted at CPU vkQueuePresentKHR begin',
        handoff_definition='after the existing pacing phase and copied fence; not earliest GPU completion time',
        observations='Body stationary in12..17; first right displacement4px in18; then8/12/16px in19/20/21. Snapshot18 was ready but not submitted. Snapshot19 is the first submitted right-response image. Shield blink phases are not mixed into a single template.',
        display_reference='144 Hz / screen center / THEORY ONLY; no measured photons',
        unmeasured=['actual_emulated_CPU_port_load','physical_switch_USB','Windows_SDL_frontend',
                    'OS_display','panel_light','same_build_pixel_off_control','other_commands_first_response'])
    return result


def main():
    import numpy as np
    base=ROOT/'.local/input-latency-v080-r1'
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=base/'native-ketsui-manual-first-response-r1.json')
    args=parser.parse_args()
    output=args.output.resolve()
    if not output.is_relative_to(base) or output.exists():
        raise ValueError('Fresh private report destination required')
    trace_path=base/'evac-ketsui-pixels-r2-trace.csv'
    pair_path=base/'ketsui-pixels-core-input-r2.json'
    chain=json.loads(pair_path.read_text())
    for source in chain['sources']:
        if digest(Path(source['path']))!=source['sha256']:
            raise ValueError('Original input/caller evidence changed')
    trace=read_trace(trace_path.read_text())
    current=analyze(trace,main_base=chain['main_image_base'])
    if current['samples']!=chain['samples']:
        raise ValueError('Recomputed core pairing differs')
    prefix=base/'evac-ketsui-pixels-r2-pixels'
    capture=read_capture(prefix,trace)
    images={i:np.asarray(rgb(capture,i)) for i in range(12,22)}
    def tracks(box,indexes):
        x,y,w,h=box;found={}
        for index in indexes:
            template=images[14 if index%2==0 else 15][y:y+h,x:x+w]
            found[index]=locate(images[index],template,y,180,280)
        return found
    primary=tracks((205,238,28,18),range(12,22))
    secondary=tracks((213,225,14,16),(12,14,16,18,20))
    sample,=[s for s in chain['samples'] if s['command']==101]
    result=reviewed_response(trace,capture,sample,primary,secondary)
    paths=[Path(__file__),ROOT/'scripts/vulkan_pixel_probe.py',ROOT/'scripts/futari_pixel_response.py',
        ROOT/'scripts/ketsui_core_latency.py',trace_path,pair_path,Path(str(prefix)+'.bin'),Path(str(prefix)+'.csv'),
        base/'ketsui-pixel-first-review-r2.png',base/'evac-ketsui-pixels-r2-launch.json',
        base/'evac-ps4-build-r9/build-result.json',*map(lambda x:Path(x['path']),chain['sources'])]
    result['sources']=[dict(path=str(p),sha256=digest(p)) for p in dict.fromkeys(paths)]
    write_json_new(output,result)
    print(json.dumps({k:v for k,v in result.items() if k not in ('sources','primary_tracks','secondary_even_phase_tracks')},indent=2))
    print('sha256',digest(output))


if __name__=='__main__': main()
