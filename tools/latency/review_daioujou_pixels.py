"""Revalidate one inspected Daioujou Stage1/Type-A/Shotia response.

Two non-overlapping body features corroborate the manual visual review. Only
this explicit neighborhood is accepted; later game-over frames are not samples.
"""
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


def response_indices(primary,secondary,frames):
    indexes=list(range(15,23))
    if set(primary)!=set(indexes) or set(secondary)!=set(indexes):
        raise ValueError('Inspected body neighborhood differs')
    for feature in (primary,secondary):
        if any(p['score']<.8 or p['score']-p['distant_score']<.025 for p in feature.values()):
            raise ValueError('Uncertain body match')
        if len({feature[i]['x'] for i in range(15,20)})!=1:
            raise ValueError('Body already moving before the tested response')
        if [feature[i]['x']-feature[19]['x'] for i in (20,21,22)]!=[4,10,14]:
            raise ValueError('Inspected first step and continuation differ')
    if frames[20]['present_begin'] or not frames[21]['present_begin'] or frames[21]['result']:
        raise ValueError('First response replacement/submission identity differs')
    return 20,21


def main():
    import numpy as np
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();base=ROOT/'.local/input-latency-v080-r1'
    if not args.output.resolve().is_relative_to(base) or args.output.exists():
        raise ValueError('Fresh private receipt required')
    trace_path=base/'evac-daioujou-pixels-r3-trace.csv'
    chain_path=base/'daioujou-core-input-r3.json';chain=json.loads(chain_path.read_text())
    for item in chain['sources']:
        if digest(Path(item['path']))!=item['sha256']: raise ValueError('Original input evidence changed')
    trace=read_trace(trace_path.read_text())
    current=analyze(trace,chain['main_image_base'],{0x27c04,0x1b55e})
    if current['samples']!=chain['samples']: raise ValueError('Recomputed source/core ownership differs')
    prefix=base/'evac-daioujou-pixels-r3-pixels';capture=read_capture(prefix,trace)
    images={i:np.asarray(rgb(capture,i)) for i in range(15,23)}
    def tracks(box):
        x,y,w,h=box;template=images[19][y:y+h,x:x+w]
        return {i:locate(im,template,y,540,650) for i,im in images.items()}
    primary=tracks((576,239,12,14));secondary=tracks((574,254,16,8))
    first,submitted=response_indices(primary,secondary,capture['frames'])
    sample,=[s for s in current['samples'] if s['command']==101]
    if sample['source_buttons']!=0x4008 or sample['raw_digital_buttons']!=0x101:
        raise ValueError('Not the inspected right/Square original digital report')
    by_id={r['seq']:r for r in trace['records']}
    source=by_id[sample['points']['source']];delivery=by_id[sample['points']['delivery_end']]
    api=by_id[sample['points']['pad_return']]
    baseline=capture['frames'][19];a=capture['frames'][first];b=capture['frames'][submitted]
    first_ready=by_id[a['ready']];shown_ready=by_id[b['ready']];present=by_id[b['present_begin']]
    release,=[r for r in trace['records'] if r['kind']==14 and r['a']==101 and not r['c']]
    if not (by_id[baseline['prepare']]['qpc']<source['qpc']<=api['qpc']<=delivery['qpc']<
            by_id[a['prepare']]['qpc']<=first_ready['qpc']<=shown_ready['qpc']<=present['qpc']):
        raise ValueError('Reviewed response no longer brackets the actual input')
    def ms(a,b): return (b['qpc']-a['qpc'])*1000/trace['frequency']
    result=dict(game='daioujou',scene='ARCADE / Type-A / Shotia / Stage1A',
        method='Explicit visual review, corroborated by disjoint cockpit and tail features, unchanged NCC gates',
        accepted_responses=1,command=101,baseline_frames=list(range(15,20)),
        first_response_snapshot=first,first_response_snapshot_presented=False,
        first_submitted_response_snapshot=submitted,response_present_seq=present['seq'],
        source_seq=source['seq'],delivery_end_seq=delivery['seq'],
        first_response_ready_seq=first_ready['seq'],submitted_response_ready_seq=shown_ready['seq'],
        source_to_core_delivery_ms=ms(source,delivery),
        core_delivery_to_first_response_handoff_ms=ms(delivery,first_ready),
        first_response_to_submitted_response_handoff_ms=ms(first_ready,shown_ready),
        submitted_response_handoff_to_present_ms=ms(shown_ready,present),
        core_delivery_to_response_present_ms=ms(delivery,present),
        pad_return_to_response_present_ms=ms(api,present),
        synthetic_source_to_response_present_ms=ms(source,present),
        response_handoff_before_release=first_ready['qpc']<release['qpc'],
        primary_tracks=primary,secondary_tracks=secondary,
        observations='Ship stationary horizontally in15..19; first right4px in20,10px in21,14px in22. Snapshot20 ready but not presented;21 is first submitted response. Ending CU still shows live Stage1 with2 ships.',
        handoff_definition='after original pacing and copied fence, not earliest GPU completion',
        p95_ms=None,display_reference='144 Hz / screen center / THEORY; panel delay unknown',
        unmeasured=['physical_switch_USB','Windows_SDL_frontend','actual_emulated_CPU_port_load',
                    'OS_display','panel_light','same_build_pixel_off_control','other_commands_first_response'])
    paths=[Path(__file__),ROOT/'scripts/daioujou_core_latency.py',ROOT/'scripts/futari_pixel_response.py',
        ROOT/'scripts/vulkan_pixel_probe.py',trace_path,chain_path,Path(str(prefix)+'.bin'),Path(str(prefix)+'.csv'),
        base/'evac-daioujou-pixels-r3-launch.json',base/'evac-daioujou-build-r2/build-result.json',
        base/'daioujou-r3-overview.png',*map(lambda x:Path(x['path']),chain['sources'])]
    result['sources']=[dict(path=str(p),sha256=digest(p)) for p in dict.fromkeys(paths)]
    write_json_new(args.output,result)
    print(json.dumps({k:v for k,v in result.items() if k not in ('sources','primary_tracks','secondary_tracks')},indent=2))
    print('sha256',digest(args.output))


if __name__=='__main__': main()
