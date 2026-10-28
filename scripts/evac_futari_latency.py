"""Join private Futari QPC probes to the native GPU's packet-tagged output log.

The production log clock is FrameTiming::NowMicros (steady_clock/QPC on this
pinned Windows toolchain). Require a separately measured clock check; never fit
an offset to make the game events line up. Log timestamps have 1 us precision.
"""
import argparse
import json
from pathlib import Path
import re

from futari_latency import analyze
from latency_probe import read_trace
from project import digest, write_json_new

LINE = re.compile(r'\[t(\d+)\] OUTPUT_LATENCY (.+)')
FIELD = re.compile(r'(\w+)=([^ ]+)')


def merge_output(trace, texts, clock):
    if (not clock.get('valid') or clock.get('samples', 0) < 100 or
            clock.get('frequency') != trace['frequency'] or
            clock.get('mapping') != 'floor(qpc*1000000/frequency)' or
            clock.get('failures') != 0):
        raise ValueError('A valid measured QPC / FrameTiming clock check is required')
    if not trace['complete']:
        raise ValueError('Incomplete native probe')
    rows = [dict(r) for r in trace['records']]
    if any(r['kind'] in (6, 7, 8, 9) for r in rows):
        raise ValueError('Native probe already contains output events')
    sequence = max((r['seq'] for r in rows), default=0)
    logs = []
    for text in texts:
        for line in text.splitlines():
            if 'OUTPUT_LATENCY' not in line:
                continue
            found = LINE.search(line)
            if not found:
                raise ValueError('Unrecognized output log line')
            fields = dict(FIELD.findall(found[2]))
            if fields.get('phase') not in ('swap', 'ready', 'present'):
                raise ValueError('Unknown output phase')
            logs.append((int(fields['t_us']), int(found[1]), fields))
    if not logs:
        raise ValueError('No native output provenance')
    # The game can keep producing output during teardown after the host trace
    # has stopped. Restrict both streams to their common observed interval;
    # still reject missing ownership anywhere INSIDE that interval.
    begin_qpc, end_qpc = min(r['qpc'] for r in rows), max(r['qpc'] for r in rows)
    included = [r for r in logs if begin_qpc <= r[0] * trace['frequency'] // 1000000 <= end_qpc]
    outside = len(logs) - len(included)
    logs = included
    swaps, ready, paints, frames = {}, {}, set(), set()

    def add(kind, micros, thread, *args):
        nonlocal sequence
        sequence += 1
        values = dict(zip(('a', 'b', 'c', 'd', 'e', 'f'), (*args, 0, 0, 0, 0, 0, 0)))
        rows.append(dict(seq=sequence, qpc=micros * trace['frequency'] // 1000000,
                         thread=thread, kind=kind, **values))
        return sequence

    for micros, thread, f in sorted(logs, key=lambda r: (r[0], {'swap': 0, 'ready': 1, 'present': 2}[r[2]['phase']])):
        frame, packet = int(f['frame']), int(f['packet'], 16)
        key = frame, packet
        if not frame or not packet:
            raise ValueError('Missing output identity')
        if f['phase'] == 'swap':
            if key in swaps or frame in frames:
                raise ValueError('Duplicate swap identity')
            frames.add(frame)
            # The guest probe records the physical packet address; other native
            # fields are not observed at that site, so never invent dimensions.
            swaps[key] = add(6, micros, thread, packet)
        elif f['phase'] == 'ready':
            if key not in swaps or key in ready:
                raise ValueError('Missing swap or duplicate ready identity')
            if f['active'] not in ('true', 'false'):
                raise ValueError('Invalid active state')
            ready[key] = add(7, micros, thread, swaps[key], int(f['mailbox']), 0, 0,
                             int(f['active'] == 'true'))
        else:
            paint = int(f['paint'])
            if key not in ready or paint in paints:
                raise ValueError('Missing ready or duplicate paint identity')
            paints.add(paint)
            if f['guest_drawn'] not in ('true', 'false') or int(f['begin_us']) > micros:
                raise ValueError('Invalid native Present')
            start = add(8, int(f['begin_us']), thread, ready[key], 0, 0,
                        int(f['guest_drawn'] == 'true'), paint)
            add(9, micros, thread, start, ready[key], int(f['hr'], 16))
    rows.sort(key=lambda r: (r['qpc'], r['seq']))
    return dict(trace, records=rows, committed=len(rows), reserved=len(rows),
                capacity=max(trace['capacity'], len(rows)), output_log_records=len(logs),
                output_log_outside_probe_interval=outside)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--logs', type=Path, nargs='+', required=True)
    parser.add_argument('--clock', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--first-command', type=int, default=101)
    parser.add_argument('--last-command', type=int, default=160)
    args = parser.parse_args()
    trace = read_trace(args.trace.read_text(encoding='utf-8'))
    combined = merge_output(trace, [p.read_text(encoding='utf-8') for p in args.logs],
                            json.loads(args.clock.read_text(encoding='utf-8')))
    report = analyze(combined, args.first_command, args.last_command)
    report['native_capture'] = {k: trace[k] for k in ('pid', 'committed', 'complete', 'overflow', 'incomplete')}
    report['output_log_records'] = combined['output_log_records']
    report['output_log_outside_probe_interval'] = combined['output_log_outside_probe_interval']
    report['sources'] = [dict(path=str(p.resolve()), sha256=digest(p))
                         for p in (args.trace, args.clock, *args.logs)]
    report['clock_precision_us'] = 1
    report['guest_submit_boundary'] = 'entry to VdSwap; Xenia probe is at VdSwap return'
    report['unmeasured'].append('native_async_output_log_observer_cost')
    write_json_new(args.output, report)
    print(json.dumps({k: v for k, v in report.items() if k != 'samples'}, indent=2))


if __name__ == '__main__':
    main()
