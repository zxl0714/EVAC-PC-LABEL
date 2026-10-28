"""Revalidate two explicitly inspected Guwange responses, retaining NCC rejects."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from evac_futari_latency import merge_output
from latency_probe import read_trace
from pixel_probe import read_capture
from project import digest, write_json_new


def present_key(event, rows, xenia):
    if xenia:
        return event['a'], event['c']
    swap = rows[rows[event['a']]['a']]
    return event['e'], swap['a']


def reviewed_response(trace, chain, capture, trackers, *, xenia, owned_index):
    if (not trace['complete'] or capture['footer']['overflow'] or
            capture['header']['pid'] != trace['pid'] or
            capture['header']['frequency'] != trace['frequency']):
        raise ValueError('Complete same-process pixel/event capture required')
    sample, = [s for s in chain['samples'] if s['command'] == 101]
    rows = {r['seq']: r for r in trace['records']}
    owned = rows[sample['points']['present_begin']]
    first_index = owned_index + 2
    if first_index+2 >= len(capture['frames']):
        raise ValueError('Missing inspected continuation images')
    neighborhood = capture['frames'][owned_index:first_index+3]
    if any(b['source_frame'] != a['source_frame']+1 for a,b in zip(neighborhood,neighborhood[1:])):
        raise ValueError('Reviewed outputs are not consecutive original frames')
    expected = capture['frames'][owned_index]
    if present_key(owned, rows, xenia) != (expected['source_event'], expected['source_packet']):
        raise ValueError('Reviewed starting image is not the exact core-owned output')
    checks = [next(e for e in r['examined'] if e['command'] == 101) for r in trackers]
    if len(checks) != 2 or any(c['owned_index'] != owned_index or
            c['frame_indexes'] != list(range(owned_index-3, owned_index+4)) for c in checks):
        raise ValueError('Reviewed response neighborhood differs')
    # Both feature trackers still locate the stationary body through owned+1.
    # The right-facing walking pose defeats NCC at owned+2; do NOT reinterpret
    # the low-confidence subsequent positions as successful tracking.
    if any(max(c['x'][:5])-min(c['x'][:5]) > 1 for c in checks):
        raise ValueError('Pre-response body position no longer supports inspection')
    if any(any(s['command'] == 101 for s in r['accepted']) for r in trackers):
        raise ValueError('This receipt describes an explicit visual, not NCC, decision')
    response = capture['frames'][first_index]
    matching = [r for r in rows.values() if r['kind'] == 8 and
                present_key(r, rows, xenia) == (response['source_event'], response['source_packet'])]
    if len(matching) != 1:
        raise ValueError('First response has no unique original output identity')
    shown = matching[0]
    if (abs(response['present_begin']-shown['qpc']) > trace['frequency']*3e-6 or
            abs(expected['present_begin']-owned['qpc']) > trace['frequency']*3e-6 or
            shown['qpc'] <= owned['qpc']):
        raise ValueError('Reviewed output clocks disagree')
    def millis(point):
        return (shown['qpc']-rows[sample['points'][point]]['qpc'])*1000/trace['frequency']
    return dict(method='Explicit visual inspection of the live Shishin body and its first right-facing movement; not an automated NCC pass',
        accepted_responses=1, command=101, owned_frame=owned_index,
        baseline_frames=list(range(first_index-3,first_index)),
        first_response_frame=first_index, next_sustained_frames=[first_index+1,first_index+2],
        owned_frame_offset=2, response_present_seq=shown['seq'],
        pre_response_feature_positions=[c['x'][:5] for c in checks],
        observation='The body stays in place through owned+1. At owned+2 it first leans and steps right, then continues right in the next two images. Later low-confidence NCC locations are not accepted measurements.',
        game_input_to_response_present_ms=millis('game_input'),
        input_api_return_to_response_present_ms=millis('input_api_return'),
        source_to_response_present_ms=millis('source'),
        owned_output_to_response_present_ms=(shown['qpc']-owned['qpc'])*1000/trace['frequency'],
        p95_ms=None, other_responses_not_visually_accepted=True,
        unmeasured=['physical_input_frontend','OS_display','panel_light','pixel_probe_off_control'],
        display_reference='144Hz / screen center / theory only')


def checked(path):
    report = json.loads(path.read_text())
    sources = report.get('sources', [report.get('source')])
    for item in sources:
        if not item or digest(Path(item['path'])) != item['sha256']:
            raise ValueError('Original observation identity changed')
    return report


def main():
    base = ROOT / '.local/input-latency-v080-r1'
    settings = (
        dict(runtime='native', prefix='evac-guwange-r3', chain='evac-guwange-cycle-r2.json',
             trackers=['evac-guwange-first-response-r3.json','evac-guwange-first-response-r4.json'],
             panel='evac-guwange-r3-first-review-r1.png', owned=17),
        dict(runtime='xenia', prefix='xenia-guwange-r6', chain='xenia-guwange-cycle-r3.json',
             trackers=['xenia-guwange-first-response-r1.json','xenia-guwange-first-response-r2.json'],
             panel='xenia-guwange-first-review-r1.png', owned=16))
    for spec in settings:
        chain_path = base / spec['chain']
        chain = checked(chain_path)
        trace_path = base / (spec['prefix']+'-trace.csv')
        trace = read_trace(trace_path.read_text())
        paths = [trace_path, chain_path]
        xenia = spec['runtime'] == 'xenia'
        if not xenia:
            sources = [Path(s['path']) for s in chain['sources']]
            logs = [p for p in sources if p.suffix == '.log']
            clock = base / 'clock-check-r1.json'
            trace = merge_output(trace, [p.read_text(encoding='utf-8') for p in logs],
                                 json.loads(clock.read_text()))
            paths += [*logs, clock]
        trackers = [base / p for p in spec['trackers']]
        result = reviewed_response(trace, chain, read_capture(base / (spec['prefix']+'-pixels')),
            [checked(p) for p in trackers], xenia=xenia, owned_index=spec['owned'])
        paths += [*trackers, base / spec['panel'],
            base / (spec['prefix']+'-pixels.csv'), base / (spec['prefix']+'-pixels.bin')]
        result['sources'] = [dict(path=str(p), sha256=digest(p)) for p in paths]
        output = base / (spec['runtime']+'-guwange-manual-first-response-r1.json')
        write_json_new(output, result)
        print(json.dumps(dict(output=str(output),sha256=digest(output),
            owned_frame_offset=result['owned_frame_offset'],
            core_to_response_ms=result['game_input_to_response_present_ms'],
            api_to_response_ms=result['input_api_return_to_response_present_ms']),indent=2))


if __name__ == '__main__':
    main()
