"""Read-only PresentMon 2.6 DXGI presentation-to-OS-display analysis.

The metric origin is NOT an input event or physical scanout. CPUBusy is the
wall interval from the inferred CPU frame start to Present entry, not summed
CPU execution time. Source definitions: PresentMon v2.6.0
IntelPresentMon/CommonUtilities/mc/MetricsCalculator{CpuGpu,Display}.cpp.
"""
import argparse
from collections import Counter, defaultdict
import csv
import math
from pathlib import Path
import statistics

from project import digest, write_json_new


REQUIRED = {'ProcessID', 'SwapChainAddress', 'PresentRuntime', 'CPUStartQPC',
            'CPUBusy', 'CPUWait', 'FrameTime', 'DisplayLatency', 'DisplayedTime',
            'PresentMode', 'SyncInterval', 'PresentFlags', 'AllowsTearing'}


def number(value, name):
    try:
        result = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f'Invalid {name}: {value!r}') from error
    if not math.isfinite(result) or result < 0:
        raise ValueError(f'Invalid {name}: {value!r}')
    return result


def distribution(values):
    if not values:
        return None
    ordered = sorted(values)

    def percentile(p):
        index = (len(ordered) - 1) * p / 100
        lo, hi = math.floor(index), math.ceil(index)
        return ordered[lo] + (ordered[hi] - ordered[lo]) * (index - lo)

    return dict(samples=len(values), min_ms=ordered[0], mean_ms=statistics.mean(values),
                p50_ms=percentile(50), p95_ms=percentile(95), p99_ms=percentile(99),
                max_ms=ordered[-1])


def analyze(rows, *, process_id, start_qpc, end_qpc, qpc_frequency):
    if process_id <= 0 or qpc_frequency <= 0 or start_qpc < 0 or end_qpc <= start_qpc:
        raise ValueError('Invalid process, QPC frequency or scene interval')
    groups = defaultdict(lambda: defaultdict(list))
    counts = Counter()
    identities = set()
    previous = None
    selected = 0
    for row in rows:
        if not REQUIRED <= row.keys():
            raise ValueError('Missing PresentMon v2 metrics: ' + ', '.join(sorted(REQUIRED - row.keys())))
        counts['source_rows'] += 1
        if int(row['ProcessID']) != process_id:
            counts['other_process_rows'] += 1
            continue
        qpc = int(row['CPUStartQPC'])
        if not start_qpc <= qpc < end_qpc:
            continue
        selected += 1
        identities.add((row['SwapChainAddress'], row['PresentRuntime']))
        if len(identities) != 1:
            raise ValueError('Select one swapchain/runtime per input file')
        if row['PresentRuntime'] != 'DXGI':
            raise ValueError('Present entry/return derivation requires DXGI instrumentation')
        if previous is not None and qpc <= previous:
            raise ValueError('Non-increasing CPUStartQPC; repeated/generated frames need separate analysis')
        previous = qpc
        if row.get('FrameType', 'Application') != 'Application':
            raise ValueError('Non-application FrameType requires separate analysis')
        busy = number(row['CPUBusy'], 'CPUBusy')
        wait = number(row['CPUWait'], 'CPUWait')
        frame = number(row['FrameTime'], 'FrameTime')
        if abs(frame - busy - wait) > 0.00031:
            raise ValueError('FrameTime does not match CPUBusy + CPUWait')
        display, duration = row['DisplayLatency'], row['DisplayedTime']
        if display == 'NA' or duration == 'NA':
            if display != duration:
                raise ValueError('Incomplete display timing pair')
            counts['not_displayed_or_unobserved_rows'] += 1
            continue
        delay = number(display, 'DisplayLatency')
        duration_ms = number(duration, 'DisplayedTime')
        if duration_ms == 0:
            counts['zero_display_duration_rows'] += 1
            continue
        start_to_display = delay - busy
        # The CSV rounds independent durations to 0.1 us. Do not silently turn
        # other impossible negative values into low-latency successes.
        if start_to_display < -0.00021:
            raise ValueError('OS display precedes Present entry')
        if start_to_display < 0:
            counts['rounding_adjusted_rows'] += 1
            start_to_display = 0.0
        display_qpc = qpc + delay * qpc_frequency / 1000
        present_end_qpc = qpc + frame * qpc_frequency / 1000
        if max(display_qpc, present_end_qpc) >= end_qpc:
            counts['crosses_scene_end_rows'] += 1
            continue
        policy = (row['PresentMode'], row['SyncInterval'], row['PresentFlags'], row['AllowsTearing'])
        group = groups[policy]
        group['present_entry_to_os_display_ms'].append(start_to_display)
        # This is signed: the frame may reach display before Present returns.
        group['present_return_to_os_display_signed_ms'].append(delay - busy - wait)
        group['present_call_ms'].append(wait)
        group['inferred_cpu_frame_start_to_os_display_ms'].append(delay)
        counts['accepted_display_rows'] += 1
    if not selected or not groups:
        raise ValueError('No observed displayed frames in selected scene')
    swapchain, runtime = next(iter(identities))
    return dict(
        schema_version=1, presentmon_version='2.6.0', process_id=process_id,
        selected_start_qpc=start_qpc, selected_end_qpc=end_qpc,
        qpc_frequency=qpc_frequency, swapchain=swapchain, runtime=runtime,
        selected_rows=selected, counts=dict(counts),
        policies=[dict(present_mode=policy[0], sync_interval=int(policy[1]),
                       present_flags=int(policy[2]), allows_tearing=int(policy[3]),
                       metrics={name: distribution(values) for name, values in metrics.items()})
                  for policy, metrics in sorted(groups.items())],
        scope='Present entry/return to OS-observed display; not game input or physical photons',
        formulas={'entry_to_display': 'DisplayLatency - CPUBusy',
                  'return_to_display_signed': 'DisplayLatency - CPUBusy - CPUWait'},
        limitations=[
            'OS display events do not measure panel response or a specific screen scanline.',
            'No input-to-image lineage or physical input time is present in this capture.',
            'NA is not zero latency; absence alone cannot distinguish a drop from missing observation.',
            'Different presentation policies remain separate groups, not a single averaged result.',
            'Application-generated duplicates cannot be detected without image/content lineage.',
            'Do not add these distributions to unrelated input/logic medians.'],
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--presentmon-version', required=True, choices=['2.6.0'])
    parser.add_argument('--process-id', required=True, type=int)
    parser.add_argument('--start-qpc', required=True, type=int)
    parser.add_argument('--end-qpc', required=True, type=int)
    parser.add_argument('--qpc-frequency', required=True, type=int)
    parser.add_argument('--scene', required=True)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    with args.capture.open(encoding='utf-8-sig', newline='') as source:
        report = analyze(csv.DictReader(source), process_id=args.process_id,
                         start_qpc=args.start_qpc, end_qpc=args.end_qpc,
                         qpc_frequency=args.qpc_frequency)
    report.update(source=str(args.capture.resolve()), source_sha256=digest(args.capture), scene=args.scene)
    write_json_new(args.output, report)
    for policy in report['policies']:
        result = policy['metrics']['present_entry_to_os_display_ms']
        print(f"{policy['present_mode']}: n={result['samples']}, "
              f"Present entry -> OS display p50={result['p50_ms']:.4f} ms, "
              f"p95={result['p95_ms']:.4f} ms, max={result['max_ms']:.4f} ms")


if __name__ == '__main__':
    main()
