"""Verify Futari sprite movement in an explicitly identified output capture.

This is an offline measurement aid. Template selection is an inspected ROI;
the output retains scores and exclusions for review, not physical display time.
"""
import argparse
from functools import lru_cache
import json
from pathlib import Path

from input_latency_report import distribution
from latency_probe import read_trace
from evac_futari_latency import merge_output
from pixel_probe import read_capture, rgb
from project import digest, write_json_new


def locate(image, template, y, x_lo, x_hi):
    import numpy as np
    height, width = template.shape[:2]
    band = image[y:y+height, x_lo:x_hi+width-1].astype(np.float64)
    target = template.astype(np.float64)
    target -= target.mean()
    target_energy = np.sum(target * target)
    if target_energy < 1e-9:
        raise ValueError('Template has no image features')
    # Each output x is one RGB patch at the same inspected vertical position.
    dot = np.zeros(x_hi-x_lo)
    for row in range(height):
        for channel in range(3):
            dot += np.correlate(band[row, :, channel], target[row, :, channel], 'valid')
    columns = band.sum(axis=(0, 2))
    squares = (band*band).sum(axis=(0, 2))
    sums = np.convolve(columns, np.ones(width), 'valid')
    sums2 = np.convolve(squares, np.ones(width), 'valid')
    energy = np.maximum(sums2 - sums*sums/(height*width*3), 1e-12)
    scores = dot / np.sqrt(energy * target_energy)
    best = int(np.argmax(scores))
    alternatives = scores.copy()
    alternatives[max(0, best-3):best+4] = -1
    return dict(x=x_lo+best, score=float(scores[best]),
                distant_score=float(alternatives.max()))


def main():
    import numpy as np
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--logs', type=Path, nargs='+', default=[])
    parser.add_argument('--clock', type=Path)
    parser.add_argument('--xenia', action='store_true')
    parser.add_argument('--sdoj-native', action='store_true')
    parser.add_argument('--chain', type=Path, required=True)
    parser.add_argument('--pixels', type=Path, required=True)
    parser.add_argument('--template', type=int, nargs=5, metavar=('FRAME','X','Y','W','H'), required=True)
    parser.add_argument('--frames', type=int, nargs=2, required=True)
    parser.add_argument('--search-x', type=int, nargs=2, default=(40, 240))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    capture = read_capture(args.pixels)
    trace = read_trace(args.trace.read_text(encoding='utf8'))
    if capture['header']['pid'] != trace['pid'] or capture['header']['frequency'] != trace['frequency']:
        raise ValueError('Pixel and event process/clock differ')
    if capture['footer']['overflow']:
        raise ValueError('Overflowed pixel capture')
    if args.xenia:
        if args.sdoj_native: raise ValueError('Choose one runtime adapter')
        if args.logs or args.clock: raise ValueError('Xenia uses its original QPC output records')
        combined = trace
    else:
        if not args.logs or not args.clock: raise ValueError('Native output logs/clock required')
        merge = merge_output
        if args.sdoj_native:
            from evac_sdoj_latency import merge_sdoj_points
            merge = merge_sdoj_points
        combined = merge(trace, [p.read_text(encoding='utf8') for p in args.logs],
                         json.loads(args.clock.read_text(encoding='utf8')))
    by_id = {r['seq']: r for r in combined['records']}
    chain = json.loads(args.chain.read_text(encoding='utf8'))
    if args.xenia and chain.get('source',{}).get('sha256') != digest(args.trace):
        raise ValueError('Xenia chain trace identity differs')
    if not args.xenia and chain['native_capture']['pid'] != trace['pid']:
        raise ValueError('Chain belongs to another process')
    paints = {}
    for f in capture['frames']:
        key = (f['source_event'], f['source_packet']) if args.xenia else f['source_event']
        if key in paints or f['hr']:
            raise ValueError('Duplicate paint or failed Present')
        paints[key] = f

    @lru_cache(maxsize=32)
    def pixels(index):
        return np.asarray(rgb(capture, index))

    reference, x, y, width, height = args.template
    template = pixels(reference)[y:y+height, x:x+width]
    if template.shape != (height, width, 3):
        raise ValueError('Invalid template')
    tracks = {}
    for index in range(args.frames[0], args.frames[1]+1):
        tracks[index] = locate(pixels(index), template, y, *args.search_x)
    accepted, excluded, examined = [], [], []
    for sample in chain['samples']:
        present = by_id[sample['points']['present_begin']]
        key = (present['a'], present['c']) if args.xenia else present['e']
        frame = paints.get(key)
        if frame is None:
            excluded.append(dict(command=sample['command'], reason='outside_pixel_capture'))
            continue
        index = frame['index']
        if abs(frame['present_begin']-present['qpc']) > trace['frequency']*3e-6:
            raise ValueError('Exact paint identity has inconsistent Present clock')
        if not args.xenia:
            swap = by_id[by_id[present['a']]['a']]
            if frame['source_packet'] != swap['a']:
                raise ValueError('Pixel paint has a different guest swap packet')
        required = list(range(index-3, index+4))
        if any(i not in tracks for i in required):
            excluded.append(dict(command=sample['command'], reason='outside_inspected_player_interval'))
            continue
        points = [tracks[i] for i in required]
        item = dict(command=sample['command'], owned_index=index,
                    x=[p['x'] for p in points], scores=[p['score'] for p in points],
                    frame_indexes=required, direction=1 if sample['buttons'] & 8 else -1)
        examined.append(item)
        if any(p['score'] < .80 or p['score']-p['distant_score'] < .025 for p in points):
            excluded.append(dict(command=sample['command'], reason='uncertain_sprite_match'))
            continue
        baseline = item['x'][:3]
        if max(baseline)-min(baseline) > 1:
            excluded.append(dict(command=sample['command'], reason='sprite_already_moving'))
            continue
        direction, start_x = item['direction'], baseline[-1]
        changes = [(i, (tracks[i]['x']-start_x)*direction) for i in required[3:]]
        first = next((i for i, shift in changes if shift >= 2), None)
        if first is None or (tracks[first]['x']-start_x)*direction > 12:
            excluded.append(dict(command=sample['command'], reason='no_unambiguous_directional_step'))
            continue
        # The accepted first image must be followed by another image moving
        # in the same direction; single-frame template animation is excluded.
        if first+1 not in tracks or (tracks[first+1]['x']-tracks[first]['x'])*direction < 2:
            excluded.append(dict(command=sample['command'], reason='movement_not_sustained'))
            continue
        response = capture['frames'][first]
        source = by_id[sample['points']['source']]
        core = by_id[sample['points']['game_input']]
        accepted.append(dict(item, first_response_index=first, frame_offset=first-index,
            source_to_response_present_us=(response['present_begin']-source['qpc'])*1e6/trace['frequency'],
            core_to_response_present_us=(response['present_begin']-core['qpc'])*1e6/trace['frequency']))
    report = dict(scope='Synthetic state -> first inspected sprite movement -> same-image Present CPU begin',
        template=args.template, inspected_frames=args.frames, search_x=args.search_x,
        captured_frames=len(capture['frames']), examined=examined,
        accepted=accepted, excluded=excluded, tracks=tracks,
        statistics={key: distribution([a[key] for a in accepted], 60) for key in
                    ('source_to_response_present_us','core_to_response_present_us')},
        unmeasured=['physical_switch_USB', 'Windows_SDL_frontend', 'display_scanout',
                    'panel_photons', 'pixel_probe_off_control'],
        sources=[dict(path=str(p.resolve()), sha256=digest(p)) for p in
                 (args.trace, args.chain, *([args.clock] if args.clock else []), *args.logs,
                  Path(str(args.pixels)+'.csv'), Path(str(args.pixels)+'.bin'))])
    write_json_new(args.output, report)
    print(json.dumps({k:report[k] for k in ('scope','statistics')}, indent=2))
    print('accepted', len(accepted), 'excluded', len(excluded))
    print('first response offsets', [s['frame_offset'] for s in accepted])


if __name__ == '__main__':
    main()
