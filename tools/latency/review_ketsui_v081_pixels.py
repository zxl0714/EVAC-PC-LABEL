"""Revalidate the inspected v081 Ketsui Stage1/Type-A right response.

This is one reviewed response, not an automatic acceptance of all input pulses.
Invulnerability animation phases use separate stationary templates. Exact
copied-image ownership, original callback delivery and Present are checked.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from futari_pixel_response import locate
from ketsui_core_latency import analyze
from latency_probe import read_trace
from project import digest, write_json_new
from vulkan_pixel_probe import read_capture, rgb


def main():
    import numpy as np
    base = ROOT / '.local/input-latency-v080-r1'
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(base) or output.exists():
        raise ValueError('Fresh private report required')
    trace_path = base / 'v081-ketsui-latency-r4-trace.csv'
    core_path = base / 'v081-ketsui-latency-r4-core.json'
    core = json.loads(core_path.read_text())
    for source in core['sources']:
        if digest(Path(source['path'])) != source['sha256']:
            raise ValueError('Original callback or capture evidence changed')
    trace = read_trace(trace_path.read_text())
    if analyze(trace, main_base=core['main_image_base'])['samples'] != core['samples']:
        raise ValueError('Original callback pairing changed')
    capture = read_capture(base / 'v081-ketsui-latency-r4-pixels', trace)
    images = {i: np.asarray(rgb(capture, i)) for i in range(12, 23)}

    def tracks(box, indices):
        x, y, width, height = box
        found = {}
        for index in indices:
            template = images[16 if index % 2 == 0 else 17][y:y+height, x:x+width]
            point = locate(images[index], template, y, 150, 235)
            if point['score'] < .8 or point['score'] - point['distant_score'] < .025:
                raise ValueError('Ambiguous inspected body feature')
            found[index] = point
        return found

    # Cockpit and lower fuselage are spatially disjoint; neither shield outline
    # nor scrolling background establishes the response.
    primary = tracks((187, 160, 8, 12), range(12, 21))
    secondary = tracks((184, 178, 12, 10), (12, 14, 16, 18, 20, 22))
    if ({primary[i]['x'] for i in range(12, 20)} != {187} or
            {secondary[i]['x'] for i in (12, 14, 16, 18)} != {184} or
            primary[20]['x'] != 191 or secondary[20]['x'] != 188 or
            secondary[22]['x'] != 196):
        raise ValueError('Inspected stationary neighborhood/right displacement changed')
    sample, = [s for s in core['samples'] if s['command'] == 101]
    if (sample['source_buttons'], sample['core_buttons']) != (0x4008, 0x101):
        raise ValueError('Not the inspected original right plus Square input')
    records = {r['seq']: r for r in trace['records']}
    source = records[sample['points']['source']]
    pad = records[sample['points']['pad_return']]
    delivery = records[sample['points']['delivery_end']]
    frames = capture['frames']
    first = frames[20]
    if not first['present_begin'] or first['result']:
        raise ValueError('First response was not successfully submitted')
    prepare, ready, present = (records[first[name]] for name in
                               ('prepare', 'ready', 'present_begin'))
    prior = records[frames[19]['prepare']]
    release, = [r for r in trace['records'] if r['kind'] == 14 and r['a'] == 101 and r['c'] == 0]
    if not source['qpc'] <= prior['qpc'] < pad['qpc'] <= delivery['qpc'] <= prepare['qpc'] <= ready['qpc'] <= present['qpc'] < release['qpc']:
        raise ValueError('Input consumption, stationary image and response ownership disagree')
    def ms(a, b):
        return (b['qpc'] - a['qpc']) * 1000 / trace['frequency']
    result = dict(accepted_responses=1, command=101,
        first_response_snapshot=20, first_submitted_response_snapshot=20,
        source_seq=source['seq'], pad_return_seq=pad['seq'], delivery_end_seq=delivery['seq'],
        response_prepare_seq=prepare['seq'], response_ready_seq=ready['seq'],
        response_present_seq=present['seq'],
        synthetic_source_to_pad_return_ms=ms(source, pad),
        pad_return_to_core_delivery_ms=ms(pad, delivery),
        core_delivery_to_response_present_ms=ms(delivery, present),
        pad_return_to_response_present_ms=ms(pad, present),
        synthetic_source_to_response_present_ms=ms(source, present),
        prepare_to_present_ms=ms(prepare, present),
        ready_to_present_ms=ms(ready, present),
        primary_tracks=primary, secondary_even_tracks=secondary,
        scope='One inspected first right-moving image, original input delivery to owned vkQueuePresentKHR begin',
        unmeasured=['physical_switch_USB', 'Windows_SDL_frontend', 'same_image_OS_adoption',
                    'monitor_scanout', 'panel_photons', 'same_build_pixel_off_control', 'other_responses'])
    paths = [Path(__file__), trace_path, core_path,
        base/'v081-ketsui-latency-r4-pixels.bin', base/'v081-ketsui-latency-r4-pixels.csv',
        base/'v081-ketsui-latency-r4-launch.json', base/'v081-ketsui-latency-r4-first-sheet.png',
        base/'v081-ketsui-r4-body-detail.png']
    result['sources'] = [dict(path=str(p), sha256=digest(p)) for p in paths]
    write_json_new(output, result)
    print(json.dumps({k:v for k,v in result.items() if k not in
                     ('primary_tracks', 'secondary_even_tracks', 'sources')}, indent=2))


if __name__ == '__main__':
    main()
