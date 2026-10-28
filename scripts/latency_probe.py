"""Validate opt-in QPC traces and join explicitly identified API/output events.

Output-ready ownership is not input-to-image provenance. This report never
assigns a keyboard event to the next frame and never calls Present a display.
"""
import argparse
from bisect import bisect_left, bisect_right
from collections import Counter
import csv
import io
import json
from pathlib import Path
import re

from input_latency_report import distribution
from project import digest, write_json_new

HEADER = re.compile(r'# evac_latency_v1 frequency=(\d+) pid=(\d+) capacity=(\d+)')
FOOTER = re.compile(r'# end reserved=(\d+) committed=(\d+) incomplete=(\d+) overflow=(\d+)')
COLUMNS = ['seq', 'qpc', 'thread', 'kind', 'a', 'b', 'c', 'd', 'e', 'f']


def read_trace(text):
    lines = text.splitlines()
    if len(lines) < 3 or not (header := HEADER.fullmatch(lines[0])):
        raise ValueError('Missing trace identity/clock header')
    if not (footer := FOOTER.fullmatch(lines[-1])):
        raise ValueError('Missing completion footer: capture did not flush normally')
    frequency, pid, capacity = map(int, header.groups())
    reserved, committed, incomplete, overflow = map(int, footer.groups())
    if not frequency or not pid or not capacity:
        raise ValueError('Invalid clock/process/capacity')
    reader = csv.DictReader(io.StringIO('\n'.join(lines[1:-1])))
    if reader.fieldnames != COLUMNS:
        raise ValueError('Unknown trace columns')
    records = []
    seen, thread_clocks = set(), {}
    for raw in reader:
        if any(not isinstance(raw.get(key), str) or not raw[key].isdigit() for key in COLUMNS):
            raise ValueError('Malformed trace record')
        row = {key: int(raw[key]) for key in COLUMNS}
        if (row['seq'] in seen or not 1 <= row['seq'] <= min(reserved, capacity) or
                not 1 <= row['kind'] <= 27 or not row['thread']):
            raise ValueError('Invalid/duplicate event identity')
        if row['qpc'] < thread_clocks.get(row['thread'], 0):
            raise ValueError('Non-monotonic thread clock')
        seen.add(row['seq'])
        thread_clocks[row['thread']] = row['qpc']
        records.append(row)
    if (committed != len(records) or committed + incomplete != min(reserved, capacity) or
            overflow != max(0, reserved - capacity)):
        raise ValueError('Trace accounting does not reconcile')
    return dict(frequency=frequency, pid=pid, capacity=capacity, reserved=reserved,
                committed=committed, incomplete=incomplete, overflow=overflow,
                complete=incomplete == 0 and overflow == 0, records=records)


def summarize_trace(trace):
    rows = trace['records']
    by_id = {row['seq']: row for row in rows}
    errors, outputs, inputs = [], [], []
    input_begin_ids = set()
    present_begin_ids = set()
    presented_ready = set()
    hz = trace['frequency']
    submits_by_swap, submit_errors = guest_submits(rows)

    def resolve(ref, kind):
        row = by_id.get(ref)
        return row if row and row['kind'] == kind else None

    def duration(left, right):
        if left['qpc'] > right['qpc']:
            raise ValueError('Linked events have reversed timestamps')
        return (right['qpc'] - left['qpc']) * 1_000_000 / hz

    for end in rows:
        if end['kind'] not in (4, 9):
            continue
        begin = resolve(end['a'], 3 if end['kind'] == 4 else 8)
        if not begin or begin['thread'] != end['thread']:
            errors.append(dict(seq=end['seq'], reason='missing_or_wrong_begin'))
            continue
        elapsed = duration(begin, end)
        if end['kind'] == 4:
            if begin['seq'] in input_begin_ids:
                raise ValueError('Duplicate input API completion')
            input_begin_ids.add(begin['seq'])
            if begin['a'] != end['b']:
                errors.append(dict(seq=end['seq'], reason='wrong_input_user'))
                continue
            inputs.append(dict(begin_seq=begin['seq'], end_seq=end['seq'],
                               user=end['b'], result=end['c'], packet=end['d'],
                               buttons=end['e'], duration_us=elapsed))
            continue
        if begin['seq'] in present_begin_ids:
            raise ValueError('Duplicate Present completion')
        present_begin_ids.add(begin['seq'])
        if end['b'] != begin['a']:
            errors.append(dict(seq=end['seq'], reason='changed_output_identity'))
            continue
        ready = resolve(begin['a'], 7)
        swap = resolve(ready['a'], 6) if ready else None
        if end['c'] != 0 or begin['d'] != 1:
            continue
        if not ready or not swap or ready['e'] != 1:
            errors.append(dict(seq=end['seq'], reason='missing_output_ownership'))
            continue
        if (swap['c'], swap['d']) != (ready['c'], ready['d']):
            errors.append(dict(seq=end['seq'], reason='output_size_mismatch'))
            continue
        first = ready['seq'] not in presented_ready
        presented_ready.add(ready['seq'])
        submission = submits_by_swap.get(swap['seq'])
        outputs.append(dict(swap_seq=swap['seq'], ready_seq=ready['seq'],
                            guest_submit_seq=submission['seq'] if submission else None,
                            guest_submit_to_swap_us=duration(submission, swap) if submission else None,
                            guest_submit_to_present_us=duration(submission, begin) if submission else None,
                            present_begin_seq=begin['seq'], present_end_seq=end['seq'],
                            swap_chain=begin['b'], first_present=first,
                            swap_to_ready_us=duration(swap, ready),
                            ready_to_present_us=duration(ready, begin),
                            swap_to_present_us=duration(swap, begin),
                            present_call_us=elapsed))
    first_outputs = [row for row in outputs if row['first_present']]
    ready_ids = {row['seq'] for row in rows if row['kind'] == 7 and row['e'] == 1}
    edges = keyboard_edges(trace, inputs)
    guest_observations = [row for row in rows if row['kind'] == 10]
    invalid_sites = [dict(seq=row['seq'], address=row['b'], expected=row['c'], actual=row['d'],
                          site_id=row['e']) for row in guest_observations if row['a'] == 0]
    input_clocks = {}
    for call in inputs:
        if call['result'] == 0:
            input_clocks.setdefault(call['user'], []).append(by_id[call['end_seq']]['qpc'])
    input_cadence = {}
    for user, clocks in input_clocks.items():
        clocks.sort()
        input_cadence[user] = distribution([(b - a) * 1_000_000 / hz
                                           for a, b in zip(clocks, clocks[1:])], 60)
    return dict(clock=dict(qpc_frequency=hz, pid=trace['pid']),
                capture=dict(complete=trace['complete'], records=trace['committed'],
                             incomplete=trace['incomplete'], overflow=trace['overflow']),
                keyboard_events=sum(row['kind'] == 2 for row in rows),
                keyboard_responses=edges,
                keyboard_coverage=keyboard_coverage(edges),
                synthetic_guest_responses=synthetic_edges(trace, inputs),
                dxgi_feedback=dxgi_feedback(rows),
                input_api_return_intervals=input_cadence,
                guest_observations=dict(counts=dict(Counter(row['a'] for row in guest_observations)),
                                        invalid_sites=invalid_sites),
                input_calls=inputs, output_responses=outputs, invalid_links=errors,
                guest_submit_pairing_errors=submit_errors,
                ready_without_successful_present=sorted(ready_ids - presented_ready),
                statistics={name: distribution([row[name] for row in first_outputs
                                                if row[name] is not None], 60)
                            for name in ('swap_to_ready_us', 'ready_to_present_us',
                                         'swap_to_present_us', 'present_call_us',
                                         'guest_submit_to_present_us')},
                scope='Explicit swap packet / output mailbox / Present ownership only',
                unmeasured=['physical_input', 'game_consumption_to_result_image',
                            'input_to_present', 'first_display', 'panel_photons'])


def keyboard_coverage(edges):
    """Keep misses/censoring and different bindings visible beside timings.

    A very short pulse is only observed when it happens to overlap a read.
    Timings conditional on that overlap are not an unbiased latency sample.
    """
    bindings = {}
    for edge in edges:
        group = bindings.setdefault(edge['binding'], [])
        group.append(edge)
    return dict(
        bound_presses=len(edges), statuses=dict(Counter(e['status'] for e in edges)),
        bindings=[dict(binding=binding, presses=len(group),
            statuses=dict(Counter(e['status'] for e in group)),
            held_duration=distribution([e['duration_ms']*1000 for e in group
                                        if e['duration_ms'] is not None],60),
            observed_event_to_api_ms=[e['event_to_api_return_ms'] for e in group
                                      if e['status']=='observed_by_guest_api'])
            for binding,group in sorted(bindings.items())],
        scope='Window-handler event -> original binding -> successful guest API return only',
        warning='Observed-only times are overlap-conditioned; do not rank their median against retained taps.')


def dxgi_feedback(rows):
    """Inspect real DXGI feedback without treating query/SyncQPC as display time."""
    by_id = {row['seq']: row for row in rows}
    observations, invalid = [], []
    for row in rows:
        if row['kind'] != 17:
            continue
        stats = by_id.get(row['a'])
        begin = by_id.get(stats['a']) if stats else None
        if (not stats or stats['kind'] != 16 or not begin or begin['kind'] != 8 or
                not begin['qpc'] <= stats['qpc'] <= row['qpc'] or
                not begin['thread'] == stats['thread'] == row['thread']):
            invalid.append(row['seq'])
            continue
        observations.append(dict(seq=stats['seq'], present_begin_seq=begin['seq'],
            last_present_count=stats['b'], count_result=stats['c'],
            present_count=stats['d'], sync_qpc=stats['e'], stats_result=stats['f'],
            present_refresh_count=row['b'], sync_refresh_count=row['c'], sync_gpu=row['d'],
            populated=(stats['c'] == 0 and stats['f'] == 0 and stats['d'] > 0 and
                       stats['e'] > 0 and row['b'] > 0 and row['c'] > 0)))
    return dict(queries=len(observations), populated=sum(x['populated'] for x in observations),
                invalid_links=invalid, observations=observations,
                scope='Raw refresh statistics; SyncQPC is not the identified image display time')


def guest_submits(rows):
    pending, matched, errors = {}, {}, []
    for row in sorted(rows, key=lambda item: (item['qpc'], item['seq'])):
        if row['kind'] not in (6, 13):
            continue
        key = row['a'], row['b'], row['c'], row['d']
        if row['kind'] == 13:
            pending.setdefault(key, []).append(row)
        else:
            candidates = pending.pop(key, [])
            if len(candidates) == 1:
                matched[row['seq']] = candidates[0]
            else:
                errors.append(dict(swap_seq=row['seq'],
                                   reason='absent_guest_submit' if not candidates else 'ambiguous_packet_reuse',
                                   candidates=[item['seq'] for item in candidates]))
    return matched, errors


def keyboard_edges(trace, inputs):
    """Follow actual per-binding press IDs into successful guest API returns."""
    rows = trace['records']
    by_id = {row['seq']: row for row in rows}
    samples_by_thread = {}
    for row in rows:
        if row['kind'] == 12:
            samples_by_thread.setdefault(row['thread'], []).append(row)
    samples_by_thread = {thread: sorted(values, key=lambda item: item['qpc'])
                         for thread, values in samples_by_thread.items()}
    sample_clocks = {thread: [item['qpc'] for item in values]
                     for thread, values in samples_by_thread.items()}
    calls_by_press = {}
    for call in inputs:
        if call['result'] != 0:
            continue
        begin, end = by_id[call['begin_seq']], by_id[call['end_seq']]
        samples = samples_by_thread.get(begin['thread'], [])
        clocks = sample_clocks.get(begin['thread'], [])
        lower, upper = bisect_left(clocks, begin['qpc']), bisect_right(clocks, end['qpc'])
        for sample in samples[lower:upper]:
            if (begin['qpc'] <= sample['qpc'] <= end['qpc'] and
                    sample['c'] == call['packet'] and sample['d'] == call['user']):
                press = by_id.get(sample['a'])
                if not press or press['kind'] != 2 or press['b'] != 1:
                    continue
                calls_by_press.setdefault(press['seq'], []).append((sample, end))
    keys = sorted((row for row in rows if row['kind'] == 2), key=lambda row: (row['qpc'], row['seq']))
    last_api_qpc = {}
    for call in inputs:
        if call['result'] == 0:
            last_api_qpc[call['user']] = max(last_api_qpc.get(call['user'], 0),
                                           by_id[call['end_seq']]['qpc'])
    result = []
    for applied in rows:
        if applied['kind'] != 11 or applied['c'] != 1 or applied['d'] != applied['a']:
            continue
        press = by_id.get(applied['a'])
        if not press or press['kind'] != 2 or press['b'] != 1:
            raise ValueError('Binding refers to an absent/non-press key event')
        release = next((key for key in keys if key['a'] == press['a'] and key['b'] == 0 and
                        key['qpc'] >= press['qpc']), None)
        calls = sorted(calls_by_press.get(press['seq'], []), key=lambda pair: pair[1]['qpc'])
        item = dict(press_seq=press['seq'], virtual_key=press['a'], binding=applied['b'],
                    duration_ms=(release['qpc'] - press['qpc']) * 1000 / trace['frequency']
                        if release else None)
        if calls:
            sample, end = calls[0]
            if sample['qpc'] < applied['qpc']:
                raise ValueError('Binding sampled before application')
            item.update(status='observed_by_guest_api', sample_seq=sample['seq'],
                        api_end_seq=end['seq'],
                        event_to_api_return_ms=(end['qpc'] - press['qpc']) * 1000 / trace['frequency'])
        else:
            finished = trace['complete'] and release and last_api_qpc.get(applied['e'], 0) > release['qpc']
            item['status'] = 'not_observed_by_guest_api' if finished else 'capture_censored'
        result.append(item)
    return result


def synthetic_edges(trace, inputs):
    """Join the explicit diagnostic source, never label it as Windows input."""
    rows = trace['records']
    by_id = {row['seq']: row for row in rows}
    samples_by_thread = {}
    for row in rows:
        if row['kind'] == 15:
            source = by_id.get(row['a'])
            if not source or source['kind'] != 14 or source['qpc'] > row['qpc']:
                raise ValueError('Synthetic sample has an invalid source event')
            if source['b'] != row['d']:
                raise ValueError('Synthetic sample changed source buttons')
            samples_by_thread.setdefault(row['thread'], []).append(row)
    samples_by_thread = {thread: sorted(samples, key=lambda row: row['qpc'])
                         for thread, samples in samples_by_thread.items()}
    sample_clocks = {thread: [row['qpc'] for row in samples]
                     for thread, samples in samples_by_thread.items()}
    first_calls = {}
    for call in inputs:
        if call['result'] != 0:
            continue
        begin, end = by_id[call['begin_seq']], by_id[call['end_seq']]
        samples = samples_by_thread.get(begin['thread'], [])
        clocks = sample_clocks.get(begin['thread'], [])
        lo, hi = bisect_left(clocks, begin['qpc']), bisect_right(clocks, end['qpc'])
        for sample in samples[lo:hi]:
            if (begin['qpc'] <= sample['qpc'] <= end['qpc'] and
                    (sample['b'], sample['c'], sample['d']) ==
                    (call['user'], call['packet'], call['buttons'])):
                previous = first_calls.get(sample['a'])
                if previous is None or end['qpc'] < previous[1]['qpc']:
                    first_calls[sample['a']] = (sample, end)
    result = []
    for source in rows:
        if source['kind'] != 14 or source['c'] != 1:
            continue
        item = dict(source_seq=source['seq'], command=source['a'], buttons=source['b'],
                    input_source='synthetic_guest_controller')
        if source['seq'] in first_calls:
            sample, end = first_calls[source['seq']]
            item.update(status='observed_by_guest_api', sample_seq=sample['seq'],
                        api_end_seq=end['seq'], source_to_api_return_ms=
                        (end['qpc'] - source['qpc']) * 1000 / trace['frequency'])
        else:
            item['status'] = 'not_observed_in_capture'
        result.append(item)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = summarize_trace(read_trace(args.trace.read_text(encoding='utf-8')))
    result['source'] = dict(path=str(args.trace.resolve()), sha256=digest(args.trace))
    write_json_new(args.output, result)
    console = {key: value for key, value in result.items()
               if key not in ('input_calls', 'output_responses', 'dxgi_feedback')}
    console['dxgi_feedback'] = {key:value for key,value in result['dxgi_feedback'].items()
                                if key != 'observations'}
    print(json.dumps(console, indent=2))


if __name__ == '__main__':
    main()
