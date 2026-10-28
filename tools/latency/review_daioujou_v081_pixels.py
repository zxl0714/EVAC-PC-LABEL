"""Revalidate one inspected v081 Daioujou ARCADE/Type-A/Shotia response."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from daioujou_core_latency import analyze
from futari_pixel_response import locate
from latency_probe import read_trace
from project import digest, write_json_new
from vulkan_pixel_probe import read_capture, rgb


def main():
    import numpy as np
    base = ROOT/'.local/input-latency-v080-r1'
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(base) or output.exists():
        raise ValueError('Fresh private report required')
    stem = 'v081-daioujou-latency-r3'
    trace_path, core_path = base/(stem+'-trace.csv'), base/(stem+'-core.json')
    core = json.loads(core_path.read_text())
    for source in core['sources']:
        if digest(Path(source['path'])) != source['sha256']:
            raise ValueError('Original callback or capture evidence changed')
    trace = read_trace(trace_path.read_text())
    audit_path = base/'daioujou-core-flow-r3/report.json'
    audit = json.loads(audit_path.read_text())
    allowed = {audit['direct_callback_return'], audit['tail_callback_return']}
    if analyze(trace, allowed_returns=allowed, main_base=core['main_image_base'])['samples'] != core['samples']:
        raise ValueError('Original input/callback pairing changed')
    capture = read_capture(base/(stem+'-pixels'), trace)
    images = {i:np.asarray(rgb(capture,i)) for i in range(12,25)}
    features = []
    for x,y,width,height in ((576,239,12,14),(574,254,16,8)):
        template = images[17][y:y+height,x:x+width]
        found = {i:locate(im,template,y,540,640) for i,im in images.items()}
        if any(p['score']<.8 or p['score']-p['distant_score']<.025 for p in found.values()):
            raise ValueError('Ambiguous inspected body feature')
        if ({found[i]['x'] for i in range(12,22)}!={x} or
                [found[i]['x']-x for i in (22,23,24)] != [4,10,14]):
            raise ValueError('Inspected stationary neighborhood/right response changed')
        features.append(found)
    sample, = [s for s in core['samples'] if s['command']==101]
    if (sample['source_buttons'],sample['raw_digital_buttons']) != (0x4008,0x101):
        raise ValueError('Not the original right plus Square delivery')
    records = {r['seq']:r for r in trace['records']}
    source,pad,delivery = (records[sample['points'][key]] for key in
                           ('source','pad_return','delivery_end'))
    first = capture['frames'][22]
    if not first['present_begin'] or first['result']:
        raise ValueError('First response was not successfully submitted')
    prepare,ready,present = (records[first[key]] for key in ('prepare','ready','present_begin'))
    prior = records[capture['frames'][21]['prepare']]
    release, = [r for r in trace['records'] if r['kind']==14 and r['a']==101 and r['c']==0]
    if not source['qpc']<=prior['qpc']<pad['qpc']<=delivery['qpc']<=prepare['qpc']<=ready['qpc']<=present['qpc']<release['qpc']:
        raise ValueError('Input, stationary image and response ownership disagree')
    def ms(a,b): return (b['qpc']-a['qpc'])*1000/trace['frequency']
    result = dict(accepted_responses=1, command=101, first_response_snapshot=22,
        first_submitted_response_snapshot=22, source_seq=source['seq'],
        pad_return_seq=pad['seq'], delivery_end_seq=delivery['seq'],
        response_prepare_seq=prepare['seq'], response_ready_seq=ready['seq'], response_present_seq=present['seq'],
        synthetic_source_to_pad_return_ms=ms(source,pad), pad_return_to_core_delivery_ms=ms(pad,delivery),
        core_delivery_to_response_present_ms=ms(delivery,present), pad_return_to_response_present_ms=ms(pad,present),
        synthetic_source_to_response_present_ms=ms(source,present), prepare_to_present_ms=ms(prepare,present),
        ready_to_present_ms=ms(ready,present), primary_tracks=features[0], secondary_tracks=features[1],
        scope='One inspected first right-moving image, original input delivery to owned vkQueuePresentKHR begin',
        unmeasured=['physical_switch_USB','Windows_SDL_frontend','same_image_OS_adoption',
                    'monitor_scanout','panel_photons','same_build_pixel_off_control','other_responses'])
    paths = [Path(__file__),trace_path,core_path,audit_path,base/(stem+'-pixels.bin'),base/(stem+'-pixels.csv'),
             base/(stem+'-launch.json'),base/(stem+'-body-sheet.png')]
    result['sources'] = [dict(path=str(p),sha256=digest(p)) for p in paths]
    write_json_new(output,result)
    print(json.dumps({k:v for k,v in result.items() if k not in ('primary_tracks','secondary_tracks','sources')},indent=2))


if __name__=='__main__':
    main()
