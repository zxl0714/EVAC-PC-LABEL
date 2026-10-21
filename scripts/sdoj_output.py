"""Pair exact guest source, physical XE_SWAP packet and D3D12 Present provenance."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

from project import digest, write_json_new
from sdoj_latency import analyze, direction_edges, fields_from, input_records, records, run_logs, stats


def output_records(lines):
    result = []
    for number, line in enumerate(lines, 1):
        if 'OUTPUT_LATENCY ' not in line:
            continue
        row = fields_from(line)
        for key in ('packet', 'frontbuffer', 'hr'):
            if (match := re.search(r'\b' + key + r'=([0-9a-fA-F]{8})\b', line)):
                row[key] = int(match[1], 16)
        if not {'phase', 'frame', 'packet', 't_us'} <= row.keys():
            raise ValueError(f'Incomplete output observation at line {number}')
        row['line'] = number
        result.append(row)
    return sorted(result, key=lambda row: row['t_us'])


def associate(events, output, lineage_report):
    sources = {chain['source']['seq']: chain for chain in lineage_report['lineages']}
    submits = [row for row in events if row['phase'] == 'guest_submit']
    submitted_by_packet = defaultdict(list)
    for row in submits:
        submitted_by_packet[row['packet']].append(row)
    groups = defaultdict(list)
    issues = Counter()
    for row in output:
        groups[row['frame']].append(row)
    sequence = sorted(row['seq'] for row in events)
    gaps = [(left + 1, right - 1) for left, right in zip(sequence, sequence[1:]) if right > left + 1]
    accepted, submitted = [], []
    pending_slots, native_pairs, native_frames = {}, {}, {}
    pending_draws, incomplete_draws = defaultdict(list), set()
    for row in sorted(events, key=lambda row: row['seq']):
        if row['phase'] not in ('native_enqueue', 'native_dequeue') or not row.get('valid'):
            continue
        key = row['queue'], row['slot']
        if row['phase'] == 'native_enqueue':
            if key in pending_slots:
                issues['native_slot_reused_without_pop'] += 1
            pending_slots[key] = row
        else:
            producer = pending_slots.pop(key, None)
            consumer = row['thread'], row['queue']
            if producer and (producer['kind'], producer['frame_arg']) == (row['kind'], row['frame_arg']):
                native_pairs[row['seq']] = (producer, row)
            if row['kind'] == 0:
                if row['seq'] in native_pairs:
                    pending_draws[consumer].append(native_pairs[row['seq']])
                else:
                    incomplete_draws.add(consumer)
            elif row['kind'] == 1:
                # The original consumer executes all kind-0 draw lists, then
                # kind-1 finalizes that frame. A frame-end argument is NOT a
                # draw-list pointer. Do not identify either by pointer alone.
                if row['seq'] in native_pairs and consumer not in incomplete_draws:
                    native_frames[row['seq']] = list(pending_draws[consumer])
                pending_draws[consumer].clear()
                incomplete_draws.discard(consumer)
            else:
                pending_draws[consumer].clear()
                incomplete_draws.add(consumer)
    native_waits = defaultdict(dict)
    for row in events:
        if row['phase'] in ('native_wait_begin', 'native_wait_end'):
            native_waits[row.get('native_dequeue_seq')].setdefault(row['phase'], []).append(row)
    used_submits = set()
    for frame, observations in sorted(groups.items()):
        swaps = [row for row in observations if row['phase'] == 'swap']
        ready = [row for row in observations if row['phase'] == 'ready']
        if len(swaps) != 1 or len(ready) != 1:
            issues['missing_or_duplicate_swap_ready'] += 1
            continue
        swap, ready = swaps[0], ready[0]
        if swap['packet'] != ready['packet'] or not ready.get('active'):
            issues['invalid_ready_packet'] += 1
            continue
        matches = [row for row in submitted_by_packet[swap['packet']] if row['seq'] not in used_submits and row.get('valid') and
                   swap['t_us'] - 500000 <= row['t_us'] <= swap['t_us']]
        # A physical ring address can recur. Refuse ambiguous reuse rather than
        # choosing an arbitrary nearest timestamp or extrapolated frame offset.
        if len(matches) != 1:
            issues['ambiguous_or_missing_submit'] += 1
            continue
        submit = matches[0]
        used_submits.add(submit['seq'])
        presents = [row for row in observations if row['phase'] == 'present' and
                    row['packet'] == swap['packet'] and row.get('hr') == 0 and row.get('guest_drawn') and
                    ready['t_us'] <= row.get('begin_us', 0) <= row['t_us']]
        if not presents:
            issues['no_observed_successful_present'] += 1
            continue
        present = min(presents, key=lambda row: row['t_us'])
        if not submit['t_us'] <= swap['t_us'] <= ready['t_us']:
            issues['output_clock_order_invalid'] += 1
            continue
        spans = dict(submit_to_gpu_swap=swap['t_us'] - submit['t_us'],
                     gpu_swap_to_ready=ready['t_us'] - swap['t_us'],
                     ready_to_present_begin=present['begin_us'] - ready['t_us'],
                     present_cpu=present['t_us'] - present['begin_us'],
                     submit_to_present_return=present['t_us'] - submit['t_us'])
        submitted.append(dict(submit=submit, swap=swap, ready=ready, present=present, intervals_us=dict(spans)))
        native = native_pairs.get(submit.get('native_dequeue_seq'))
        origin = native[0] if native else submit
        chain = sources.get(origin.get('render_source_seq'))
        if not chain:
            issues['missing_source_lineage'] += 1
            continue
        source = chain['source']
        if (source['host_iteration'] != origin['host_iteration'] or source['thread'] != origin['thread'] or
                not source['t_us'] <= origin['t_us'] <= submit['t_us'] or
                any(left <= submit['seq'] and right >= source['seq'] for left, right in gaps)):
            issues['unproven_source_to_submit'] += 1
            continue
        draw_dispatch, wait_pair = None, None
        if native:
            enqueue, dequeue = native
            drawings = native_frames.get(dequeue['seq'])
            matches = [pair for pair in (drawings or []) if
                       pair[0]['frame_arg'] == source.get('draw_list') and
                       pair[0].get('render_source_seq') == source['seq'] and
                       pair[0]['thread'] == source['thread'] and
                       pair[0]['host_iteration'] == source['host_iteration'] and
                       source['t_us'] <= pair[0]['t_us'] <= enqueue['t_us'] and
                       pair[0]['t_us'] <= pair[1]['t_us'] <= dequeue['t_us']]
            if (enqueue['kind'] != 1 or dequeue['thread'] != submit['thread'] or len(matches) != 1 or
                    not enqueue['t_us'] <= dequeue['t_us'] <= submit['t_us']):
                issues['unproven_native_draw_list_dispatch'] += 1
                continue
            draw_dispatch = matches[0]
            spans.update(source_to_native_enqueue=enqueue['t_us'] - source['t_us'],
                         native_queue_wait=dequeue['t_us'] - enqueue['t_us'],
                         native_dequeue_to_submit=submit['t_us'] - dequeue['t_us'],
                         source_to_draw_enqueue=draw_dispatch[0]['t_us'] - source['t_us'],
                         draw_queue_wait=draw_dispatch[1]['t_us'] - draw_dispatch[0]['t_us'])
            waits = native_waits[dequeue['seq']]
            begins, ends = waits.get('native_wait_begin', []), waits.get('native_wait_end', [])
            if len(begins) == len(ends) == 1:
                begin, end = begins[0], ends[0]
                if (begin['thread'] == end['thread'] == submit['thread'] and end.get('wait_flag') == 0 and
                        dequeue['t_us'] <= begin['t_us'] <= end['t_us'] <= submit['t_us']):
                    wait_pair = begin, end
                    spans.update(native_before_wait=begin['t_us'] - dequeue['t_us'],
                                 native_callback_wait=end['t_us'] - begin['t_us'],
                                 native_after_wait_to_submit=submit['t_us'] - end['t_us'])
        elif submit.get('native_dequeue_seq'):
            issues['missing_native_queue_pair'] += 1
            continue
        spans.update(source_to_guest_submit=submit['t_us'] - source['t_us'],
                     source_to_present_return=present['t_us'] - source['t_us'],
                     copy_to_present_return=present['t_us'] - chain['copy']['t_us'])
        accepted.append(dict(lineage=chain, submit=submit, swap=swap, ready=ready,
                             present=present, native_queue=native, draw_dispatch=draw_dispatch,
                             native_wait=wait_pair, intervals_us=spans))
    seen_results, first_results = set(), []
    for row in accepted:
        chain = row['lineage']
        key = (chain['began']['module'], chain['began']['generation'],
               chain['began']['seq'], chain['swap']['swap_serial'])
        row['first_result_output'] = key not in seen_results
        seen_results.add(key)
        if row['first_result_output']:
            first_results.append(row)
    return dict(scope='Exact source to successful Present CPU return; not GPU completion, first pixel or physical scanout',
                paired_frames=len(accepted), issues=dict(issues), frames=accepted,
                first_result_frames=len(first_results), held_result_frames=len(accepted)-len(first_results),
                first_result_intervals_us={key: stats([row['intervals_us'][key] for row in first_results
                                                      if key in row['intervals_us']])
                                           for key in sorted({key for row in first_results for key in row['intervals_us']})},
                submitted_frames=submitted, paired_submit_to_present=len(submitted),
                submit_intervals_us={key: stats([row['intervals_us'][key] for row in submitted])
                                     for key in (submitted[0]['intervals_us'] if submitted else ())},
                intervals_us={key: stats([row['intervals_us'][key] for row in accepted if key in row['intervals_us']])
                              for key in sorted({key for row in accepted for key in row['intervals_us']})})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    paths = run_logs(args.run)
    events, inputs, output = [], [], []
    for path in paths:
        lines = path.read_text(encoding='utf-8').splitlines()
        for target, parse in ((events, records), (inputs, input_records), (output, output_records)):
            target.extend(dict(row, log=path.name) for row in parse(lines))
    events.sort(key=lambda row: (row['t_us'], row['seq']))
    inputs.sort(key=lambda row: row['t_us'])
    output.sort(key=lambda row: row['t_us'])
    lineage = analyze(events)
    result = associate(events, output, lineage)
    result['input_edges'] = []
    for edge in direction_edges(inputs, lineage, events):
        paired = next((row for row in result['frames'] if edge['copied'] and
                       row['lineage']['copy']['seq'] == edge['copied']['seq']), None)
        result['input_edges'].append(dict(edge=edge, output=paired,
            key_to_present_return_us=paired['present']['t_us'] - edge['press']['t_us'] if paired else None))
    result['sources'] = [dict(path=str(path), sha256=digest(path)) for path in paths]
    result['lineage_summary'] = {key: value for key, value in lineage.items() if key != 'lineages'}
    write_json_new(args.output, result)
    summary = {key: value for key, value in result.items()
               if key not in ('frames', 'submitted_frames', 'input_edges', 'lineage_summary')}
    summary['lineage_summary'] = {key: value for key, value in result['lineage_summary'].items()
                                  if key not in ('copy_to_source_host_iterations', 'copy_to_source_epochs')}
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
