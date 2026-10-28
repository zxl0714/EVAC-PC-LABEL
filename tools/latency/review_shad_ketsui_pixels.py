"""Revalidate the inspected shad Ketsui right step and same-image Present."""
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from futari_pixel_response import locate
from ketsui_core_latency import analyze
from latency_probe import read_trace
from project import digest,write_json_new
from shad_ketsui_core_latency import attach_sources
from vulkan_pixel_probe import read_capture,rgb


def reviewed_response(trace,capture,sample,primary,secondary):
    indexes=set(range(4,16))
    if set(primary)!=indexes or set(secondary)!=indexes:
        raise ValueError('Inspected shad response neighborhood changed')
    for feature in (primary,secondary):
        if any(p['score']<.8 or p['score']-p['distant_score']<.025 for p in feature.values()):
            raise ValueError('Uncertain shad body feature')
        baseline=[feature[i]['x'] for i in range(4,12)]
        if max(baseline)-min(baseline)>1: raise ValueError('Shad body already moving')
        if [feature[i]['x']-baseline[-1] for i in range(12,16)]!=[4,8,8,8]:
            raise ValueError('First shad right step/continued displacement differs')
    if sample['command']!=101 or sample['source_buttons']!=0x4008 or sample['core_buttons']!=0x101:
        raise ValueError('Not the inspected original right input')
    rows={r['seq']:r for r in trace['records']};frame=capture['frames'][12]
    if not frame['present_begin'] or frame['result']:
        raise ValueError('The first responsive image was not successfully submitted')
    source,delivery=(rows[sample['points'][k]] for k in ('source','delivery_end'))
    prepare,ready,present=(rows[frame[k]] for k in ('prepare','ready','present_begin'))
    if not rows[capture['frames'][11]['prepare']]['qpc']<source['qpc']<=delivery['qpc']<prepare['qpc']<=ready['qpc']<=present['qpc']:
        raise ValueError('Original input/response clock order differs')
    def ms(a,b): return (b['qpc']-a['qpc'])*1000/trace['frequency']
    return dict(method='Explicit body inspection plus two same-phase templates; one response only',
        accepted_responses=1,command=101,baseline_frames=list(range(4,12)),
        first_response_snapshot=12,first_submitted_response_snapshot=12,
        response_present_seq=present['seq'],source_seq=source['seq'],delivery_end_seq=delivery['seq'],
        primary_tracks=primary,secondary_tracks=secondary,
        synthetic_source_to_core_delivery_ms=ms(source,delivery),
        core_delivery_to_response_prepare_ms=ms(delivery,prepare),
        response_prepare_to_submission_ready_ms=ms(prepare,ready),
        submission_ready_to_present_ms=ms(ready,present),
        core_delivery_to_response_present_ms=ms(delivery,present),
        pad_return_to_response_present_ms=ms(rows[sample['points']['pad_return']],present),
        synthetic_source_to_response_present_ms=ms(source,present),p95_ms=None,
        scope='Published synthetic input / LAST original PadRead / core input delivery -> first right-moving image CPU Present begin',
        readiness_definition='shad Ready is after draw_scheduler.Flush, NOT a GPU completion or native completed handoff',
        observations='Body stationary4..11. Frame12 first right4px,13 advances8px,14/15 retain8px after release. Both features agree. Same first image12 is submitted; no replacement before this response.',
        display_reference='144 Hz / screen center / THEORY ONLY',
        unmeasured=['physical_frontend','actual_emulated_CPU_port_load','OS_display','panel_light',
                    'same_build_pixel_off_control','other_pulses_first_response'])


def main():
    import numpy as np
    base=ROOT/'.local/input-latency-v080-r1'
    trace_path=base/'shad-ketsui-core-pixels-r3-trace.csv'
    chain_path=base/'shad-ketsui-core-input-r3.json'
    chain=json.loads(chain_path.read_text())
    for source in chain['sources']:
        if digest(Path(source['path']))!=source['sha256']: raise ValueError('Original shad input evidence changed')
    trace=read_trace(trace_path.read_text())
    if analyze(attach_sources(trace),main_base=chain['main_image_base'])['samples']!=chain['samples']:
        raise ValueError('Original shad input/core pairing differs')
    prefix=base/'shad-ketsui-core-pixels-r3-pixels';capture=read_capture(prefix,trace)
    images={i:np.asarray(rgb(capture,i)) for i in range(4,16)}
    def tracks(box):
        x,y,w,h=box
        return {i:locate(images[i],images[6 if i%2==0 else 7][y:y+h,x:x+w],y,180,280) for i in images}
    sample,=[s for s in chain['samples'] if s['command']==101]
    result=reviewed_response(trace,capture,sample,tracks((205,238,28,18)),tracks((206,225,28,28)))
    paths=[Path(__file__),ROOT/'scripts/vulkan_pixel_probe.py',ROOT/'scripts/futari_pixel_response.py',
        trace_path,chain_path,Path(str(prefix)+'.csv'),Path(str(prefix)+'.bin'),
        base/'shad-ketsui-first-review-r3.png',base/'shad-ketsui-build-r5/build-result.json',
        base/'shad-ketsui-probe-r5/preparation.json',*map(lambda x:Path(x['path']),chain['sources'])]
    result['sources']=[dict(path=str(p),sha256=digest(p)) for p in dict.fromkeys(paths)]
    output=base/'shad-ketsui-manual-first-response-r1.json'
    write_json_new(output,result)
    print(json.dumps({k:v for k,v in result.items() if k not in ('sources','primary_tracks','secondary_tracks')},indent=2))
    print('sha256',digest(output))


if __name__=='__main__': main()
