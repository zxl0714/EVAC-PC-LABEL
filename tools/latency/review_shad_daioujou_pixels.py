"""Revalidate one visually inspected shad Daioujou Stage1 response."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from futari_pixel_response import locate
from latency_probe import read_trace
from project import digest,write_json_new
from shad_daioujou_core_latency import analyze_shad
from vulkan_pixel_probe import read_capture,rgb


def response_indices(primary,secondary,frames):
    indexes=list(range(6,17))
    if set(primary)!=set(indexes) or set(secondary)!=set(indexes):raise ValueError('Inspected neighborhood differs')
    for feature in (primary,secondary):
        if any(v['score']<.8 or v['score']-v['distant_score']<.025 for v in feature.values()):
            raise ValueError('Uncertain body feature')
        if len({feature[i]['x'] for i in range(6,12)})!=1:raise ValueError('Body moving before input')
        if [feature[i]['x']-feature[11]['x'] for i in range(12,17)]!=[4,10,10,10,10]:
            raise ValueError('Inspected first right response/continuation differs')
    if not frames[12]['present_begin'] or frames[12]['result']:raise ValueError('First response not submitted successfully')
    return 12


def main():
    import numpy as np
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();base=ROOT/'.local/input-latency-v080-r1'
    if not args.output.resolve().is_relative_to(base) or args.output.exists():raise ValueError('Fresh private output required')
    trace_path=base/'shad-daioujou-pixels-r3-trace.csv';chain_path=base/'shad-daioujou-core-input-r3.json'
    chain=json.loads(chain_path.read_text())
    for item in chain['sources']:
        if digest(Path(item['path']))!=item['sha256']:raise ValueError('Original source/input evidence changed')
    trace=read_trace(trace_path.read_text());current=analyze_shad(trace)
    if current['samples']!=chain['samples']:raise ValueError('Recomputed original source pairing differs')
    prefix=base/'shad-daioujou-pixels-r3-pixels';capture=read_capture(prefix,trace)
    images={i:np.asarray(rgb(capture,i)) for i in range(6,17)}
    def tracks(box):
        x,y,w,h=box;template=images[11][y:y+h,x:x+w]
        return {i:locate(im,template,y,480,590) for i,im in images.items()}
    primary=tracks((516,239,12,14));secondary=tracks((514,254,16,8))
    first=response_indices(primary,secondary,capture['frames']);frame=capture['frames'][first]
    sample,=[s for s in current['samples'] if s['command']==101]
    if sample['source_buttons']!=0x4008 or sample['raw_digital_buttons']!=0x101:raise ValueError('Different original raw input')
    by_id={r['seq']:r for r in trace['records']}
    source=by_id[sample['points']['source']];api=by_id[sample['points']['pad_return']]
    delivery=by_id[sample['points']['delivery_end']]
    prepare=by_id[frame['prepare']];ready=by_id[frame['ready']];present=by_id[frame['present_begin']]
    if not (by_id[capture['frames'][11]['prepare']]['qpc']<source['qpc']<=api['qpc']<=delivery['qpc']<
            prepare['qpc']<=ready['qpc']<=present['qpc']):raise ValueError('Inspected response/input clock boundary differs')
    def ms(a,b):return (b['qpc']-a['qpc'])*1000/trace['frequency']
    result=dict(game='daioujou',scene='ARCADE / Type-A / Shotia / Stage1A',accepted_responses=1,command=101,
        method='Explicit manual body review corroborated by disjoint cockpit/tail features with unchanged NCC gates',
        baseline_frames=list(range(6,12)),first_response_snapshot=first,first_response_snapshot_presented=True,
        response_present_seq=present['seq'],source_seq=source['seq'],delivery_end_seq=delivery['seq'],
        source_to_core_delivery_ms=ms(source,delivery),core_delivery_to_prepare_ms=ms(delivery,prepare),
        prepare_to_submission_ready_ms=ms(prepare,ready),submission_ready_to_present_ms=ms(ready,present),
        core_delivery_to_response_present_ms=ms(delivery,present),pad_return_to_response_present_ms=ms(api,present),
        synthetic_source_to_response_present_ms=ms(source,present),primary_tracks=primary,secondary_tracks=secondary,
        p95_ms=None,ready_definition='GPU submission recorded, not GPU completion',
        observations='Horizontally stationary6..11, first right4px12,10px13, retained14..16;12 is submitted. Before/after sequence CU show live Stage1, ending1ship. Different scene progress from native, one example not a population result.',
        display_reference='144 Hz / screen center / THEORY; panel delay unknown',
        unmeasured=['physical_switch_USB','Windows_SDL_frontend','actual_emulated_CPU_port_load',
                    'OS_display','panel_light','same_build_pixel_off_control','other_commands_first_response'])
    paths=[Path(__file__),ROOT/'scripts/shad_daioujou_core_latency.py',ROOT/'scripts/futari_pixel_response.py',
        ROOT/'scripts/vulkan_pixel_probe.py',trace_path,chain_path,Path(str(prefix)+'.bin'),Path(str(prefix)+'.csv'),
        base/'shad-daioujou-pixels-r3-launch.json',base/'shad-daioujou-build-r1/build-result.json',
        base/'shad-daioujou-r3-overview.png',*map(lambda x:Path(x['path']),chain['sources'])]
    result['sources']=[dict(path=str(p),sha256=digest(p)) for p in dict.fromkeys(paths)]
    write_json_new(args.output,result)
    print(json.dumps({k:v for k,v in result.items() if k not in ('sources','primary_tracks','secondary_tracks')},indent=2))
    print('sha256',digest(args.output))


if __name__=='__main__':main()
