"""Revalidate one explicitly inspected native SDOJ response; retain NCC exclusions."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from evac_sdoj_latency import merge_sdoj_points
from latency_probe import read_trace
from pixel_probe import read_capture
from project import digest, write_json_new


def reviewed_response(trace, chain, capture, trackers):
    if (not trace['complete'] or capture['footer']['overflow'] or
            capture['header']['pid'] != trace['pid'] or
            capture['header']['frequency'] != trace['frequency']):
        raise ValueError('Complete same-process pixel/event capture required')
    sample, = [s for s in chain['samples'] if s['command'] == 101]
    rows = {r['seq']: r for r in trace['records']}
    present = rows[sample['points']['present_begin']]
    swap = rows[rows[present['a']]['a']]
    frame = capture['frames'][16]
    if ((frame['source_event'], frame['source_packet']) != (present['e'], swap['a']) or
            abs(frame['present_begin'] - present['qpc']) > trace['frequency'] * 3e-6):
        raise ValueError('Reviewed image is not the exact owned Present')
    checks = [next(e for e in r['examined'] if e['command'] == 101) for r in trackers]
    if len(checks) != 2 or any(c['frame_indexes'] != list(range(13, 20)) or
                             c['owned_index'] != 16 for c in checks):
        raise ValueError('Explicitly inspected response neighborhood differs')
    offsets = [[x - c['x'][0] for x in c['x']] for c in checks]
    if (any(max(o[:3]) - min(o[:3]) > 1 for o in offsets) or
            any(not 2 <= o[3] <= 12 or o[4] - o[3] < 2 for o in offsets) or
            any(abs(a - b) > 1 for a, b in zip(*offsets))):
        raise ValueError('Body positions no longer support visual inspection')
    return dict(method='Explicit visual inspection of frames13..18 and two body-feature position checks; not an automated NCC pass',
        accepted_responses=1, command=101, baseline_frames=[13, 14, 15],
        first_response_frame=16, next_sustained_frame=17, owned_frame_offset=0,
        relative_body_positions=offsets,
        observation='The same live red ship is stationary in13..15, first moves right in16 and continues right in17..18. Both original automated NCC gates remain rejected.',
        game_input_to_response_present_ms=sample['times']['game_input_to_present_begin_us'] / 1000,
        source_to_response_present_ms=sample['times']['source_to_present_begin_us'] / 1000,
        p95_ms=None, other_responses_not_visually_accepted=True,
        unmeasured=['physical_input_frontend', 'OS_display', 'panel_light'],
        display_reference='144Hz / screen center / theory only')


def checked(path):
    report = json.loads(path.read_text())
    for item in report['sources']:
        if digest(Path(item['path'])) != item['sha256']:
            raise ValueError('Original observation identity changed')
    return report


def main():
    base = ROOT / '.local/input-latency-v080-r1'
    trace_path = base / 'evac-sdoj-r1-trace.csv'
    chain_path = base / 'evac-sdoj-onev-cycle-r2.json'
    chain = checked(chain_path)
    source_paths = [Path(s['path']) for s in chain['sources']]
    logs = [p for p in source_paths if p.suffix == '.log']
    clock = base / 'clock-check-r1.json'
    trace = merge_sdoj_points(read_trace(trace_path.read_text()),
        [p.read_text(encoding='utf-8') for p in logs], json.loads(clock.read_text()))
    tracker_paths = [base / f'evac-sdoj-first-response-r{n}.json' for n in (1, 3)]
    trackers = [checked(p) for p in tracker_paths]
    result = reviewed_response(trace, chain, read_capture(base / 'evac-sdoj-r1-pixels'), trackers)
    paths = [trace_path, chain_path, clock, *logs, *tracker_paths,
        base / 'evac-sdoj-r1-pixels.csv', base / 'evac-sdoj-r1-pixels.bin',
        base / 'evac-sdoj-first101-review-r1.png']
    result['sources'] = [dict(path=str(p), sha256=digest(p)) for p in paths]
    output = base / 'evac-sdoj-manual-first-response-r1.json'
    write_json_new(output, result)
    print(json.dumps(dict(output=str(output), sha256=digest(output),
        accepted=1, core_to_present_ms=result['game_input_to_response_present_ms']), indent=2))


if __name__ == '__main__':
    main()
