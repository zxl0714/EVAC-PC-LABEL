"""Summarize PS4 input API and owned Vulkan presentation records separately.

No input-to-image association is inferred from chronology. First-response
content evidence is required before the two halves can be joined.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path

from input_latency_report import distribution
from latency_probe import read_trace
from project import digest, write_json_new

PS4_MAPPING = (0x10, 0x40, 0x80, 0x20, 0x08, 0x100000, 0x02, 0x04,
               0x400, 0x800, 0, 0, 0x4000, 0x2000, 0x8000, 0x1000)


def ps4_buttons(xbox):
    return sum(mask for bit, mask in enumerate(PS4_MAPPING) if xbox & (1 << bit))


def summarize(trace, first_command=101, last_command=160):
    if not trace['complete']:
        raise ValueError('Incomplete PS4 capture')
    rows = trace['records']
    by_id = {r['seq']: r for r in rows}
    hz = trace['frequency']
    reports = defaultdict(list)
    input_ends, present_ends, ready_ids = set(), set(), set()
    inputs, outputs, observed = [], [], {}
    controller_samples = {}
    filtered = Counter()

    def resolve(ref, kind):
        row = by_id.get(ref)
        if not row or row['kind'] != kind:
            raise ValueError('Missing or incorrect PS4 ownership record')
        return row

    def duration(begin, end):
        if end['qpc'] < begin['qpc']:
            raise ValueError('Reversed PS4 event clocks')
        return (end['qpc'] - begin['qpc']) * 1_000_000 / hz

    for row in rows:
        if row['kind'] == 27:
            source = resolve(row['b'], 14)
            duration(source, row)
            if not row['a'] or not row['c'] or row['d'] != ps4_buttons(source['b']):
                raise ValueError('Invalid queued controller source')
            key = (row['a'], row['c'], row['d'])
            previous = controller_samples.get(key)
            if previous and previous['b'] != row['b']:
                raise ValueError('Ambiguous controller timestamp/source')
            controller_samples[key] = row
        if row['kind'] == 20:
            begin = resolve(row['a'], 18)
            duration(begin, row)
            if row['thread'] != begin['thread'] or row['b'] >= begin['b']:
                raise ValueError('Pad report belongs to another call or exceeds capacity')
            reports[begin['seq']].append(row)

    for row in rows:
        if row['kind'] == 19:
            begin = resolve(row['a'], 18)
            if begin['seq'] in input_ends or begin['thread'] != row['thread']:
                raise ValueError('Duplicate or cross-thread PadRead completion')
            input_ends.add(begin['seq'])
            returned = sorted(reports[begin['seq']], key=lambda r: r['b'])
            if row['c'] > begin['b'] or [r['b'] for r in returned] != list(range(row['c'])):
                raise ValueError('Missing/duplicate PadRead report')
            if row['b'] != (0 if begin['e'] else row['c']):
                raise ValueError('Unexpected scePadRead result/count')
            inputs.append(dict(begin=begin['seq'], end=row['seq'], reports=row['c'],
                               api_us=duration(begin, row)))
            for report in returned:
                duration(report, row)
                source_id = report['f']
                if not source_id and begin['f']:
                    sampled = controller_samples.get((begin['f'], report['d'], report['c']))
                    if sampled:
                        duration(sampled, report)
                        source_id = sampled['b']
                if not report['e'] or not source_id:
                    continue
                source = resolve(source_id, 14)
                if report['c'] != ps4_buttons(source['b']):
                    raise ValueError('Synthetic source does not match returned buttons')
                if source['c'] != 1 or not first_command <= source['a'] <= last_command:
                    continue
                candidate = dict(command=source['a'], source=source['seq'],
                    report=report['seq'], api_end=row['seq'], buttons=report['c'],
                    source_to_api_return_us=duration(source, row))
                previous = observed.get(source['seq'])
                if previous is None or by_id[previous['api_end']]['qpc'] > row['qpc']:
                    observed[source['seq']] = candidate
        elif row['kind'] == 23:
            begin = resolve(row['a'], 22)
            if begin['seq'] in present_ends or begin['thread'] != row['thread']:
                raise ValueError('Duplicate or cross-thread vkQueuePresent completion')
            present_ends.add(begin['seq'])
            call_us = duration(begin, row)
            if row['b'] != begin['a']:
                raise ValueError('Present ownership changed during call')
            if row['c'] not in (0, 1000001003):
                filtered['failed_present'] += 1
                continue
            if not begin['a'] or not begin['e'] or begin['f']:
                filtered['blank_ui_or_reused_image'] += 1
                continue
            ready = resolve(begin['a'], 21)
            prepare = resolve(ready['a'], 26)
            if ready['c'] != prepare['a'] or not ready['b'] or not ready['d'] or ready['f'] != 1:
                raise ValueError('Prepared image identity mismatch')
            duration(prepare, ready)
            duration(ready, begin)
            if ready['seq'] in ready_ids:
                filtered['duplicate_image_present'] += 1
                continue
            ready_ids.add(ready['seq'])
            outputs.append(dict(prepare=prepare['seq'], ready=ready['seq'],
                present_begin=begin['seq'], present_end=row['seq'],
                guest_address=ready['c'], image=ready['d'],
                swapchain=begin['b'], image_index=begin['c'], present_mode=begin['d'],
                prepare_to_ready_us=duration(prepare, ready),
                ready_to_present_us=duration(ready, begin),
                prepare_to_present_us=duration(prepare, begin), present_call_us=call_us))

    sources = [r for r in rows if r['kind'] == 14 and r['c'] == 1 and
               first_command <= r['a'] <= last_command]
    if len({r['a'] for r in sources}) != len(sources):
        raise ValueError('Reused test command identity')
    unpaired = [r['a'] for r in sources if r['seq'] not in observed]
    # Navigation and warm-up remain in the raw capture, not in the measured
    # presentation distribution. Bound the latter by actual publish/release
    # events, never by the requested sleep duration or process lifetime.
    window = None
    measured_outputs = []
    if sources:
        last_source = max(sources, key=lambda r: r['qpc'])
        releases = [r for r in rows if r['kind'] == 14 and r['c'] == 0 and
                    r['a'] == last_source['a'] and r['qpc'] > last_source['qpc']]
        if releases:
            window = dict(begin_qpc=min(r['qpc'] for r in sources),
                          end_qpc=min(r['qpc'] for r in releases))
            measured_outputs = [r for r in outputs if
                by_id[r['prepare']]['qpc'] >= window['begin_qpc'] and
                by_id[r['present_end']]['qpc'] <= window['end_qpc']]
    stats = {'synthetic_publish_to_pad_api_return_us': distribution(
        [r['source_to_api_return_us'] for r in observed.values()], 60)}
    for key in ('prepare_to_ready_us', 'ready_to_present_us', 'prepare_to_present_us', 'present_call_us'):
        stats[key] = distribution([r[key] for r in measured_outputs], 60)
    return dict(capture={k: trace[k] for k in ('pid', 'committed', 'complete', 'overflow', 'incomplete')},
        input_calls=len(inputs), owned_output_images=len(outputs),
        measured_output_images=len(measured_outputs), measurement_window=window,
        requested_sources=len(sources), paired_input_sources=len(observed), unpaired_commands=unpaired,
        unfinished_pad_calls=sum(r['kind'] == 18 and r['seq'] not in input_ends for r in rows),
        unfinished_present_calls=sum(r['kind'] == 22 and r['seq'] not in present_ends for r in rows),
        filtered_outputs=dict(filtered), statistics=stats,
        input_samples=list(observed.values()), output_samples=outputs,
        scope='Two separate chains: synthetic source -> PadRead; prepared image -> vkQueuePresent',
        unmeasured=['device_or_Windows_frontend', 'game_logic_input_consumption',
                    'input_to_first_responsive_image', 'display_scanout', 'panel_photons',
                    'probe_off_control'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--first-command', type=int, default=101)
    parser.add_argument('--last-command', type=int, default=160)
    args = parser.parse_args()
    report = summarize(read_trace(args.trace.read_text(encoding='utf-8')),
                       args.first_command, args.last_command)
    report['source'] = dict(path=str(args.trace.resolve()), sha256=digest(args.trace))
    write_json_new(args.output, report)
    print(json.dumps({k: v for k, v in report.items() if not k.endswith('_samples')}, indent=2))


if __name__ == '__main__':
    main()
