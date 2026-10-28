"""Analyze explicitly identified Vulkan present timing feedback.

Stage4 is FIRST_PIXEL_OUT (presentation-engine output), not physical visibility
or panel emission. Stage8, where supported, is reported separately. QPC and
stage-local clocks are connected by calibrated timestamps including uncertainty.
"""
import argparse
from bisect import bisect_left
from collections import Counter, defaultdict
import json
import math
from pathlib import Path

from present_latency import distribution
from project import digest, write_json_new


def integer(row, name, *, positive=False):
    value = row[name]
    if type(value) is not int or value < int(positive):
        raise ValueError('Invalid ' + name)
    return value


def analyze(text, warmup_ms=1000):
    rows = [json.loads(line) for line in text.splitlines() if line.strip()]
    if (len(rows) < 4 or rows[0].get('kind') != 'header' or rows[-1].get('kind') != 'end'
            or rows[0].get('schema') != 1):
        raise ValueError('Complete identified feedback capture required')
    if not math.isfinite(warmup_ms) or warmup_ms < 0:
        raise ValueError('Invalid warm-up duration')
    hz = integer(rows[0], 'frequency', positive=True)
    pid = integer(rows[0], 'pid', positive=True)
    invalid_calibrations = 0
    presents, work, stages, calibrations, chains = {}, {}, {}, defaultdict(list), {}
    for row in rows[1:-1]:
        generation = integer(row, 'generation', positive=True)
        kind = row['kind']
        if kind == 'chain':
            if generation in chains:
                raise ValueError('Duplicate chain generation')
            mask = integer(row, 'stages', positive=True)
            if mask & ~13 or not mask & 12:
                raise ValueError('Missing output/visibility stage')
            chains[generation] = row
        elif kind in ('present', 'work'):
            key = generation, integer(row, 'id', positive=True)
            target = presents if kind == 'present' else work
            if key in target:
                raise ValueError('Duplicate ' + kind + ' identity')
            if kind == 'present':
                if integer(row, 'end_qpc') < integer(row, 'begin_qpc'):
                    raise ValueError('Reversed Present call')
                if row['result'] not in (0, 1000001003):
                    raise ValueError('Failed Present in benchmark')
            else:
                clocks = [integer(row, key) for key in ('begin_qpc', 'acquired_qpc', 'submitted_qpc', 'retired_qpc')]
                if clocks != sorted(clocks):
                    raise ValueError('Reversed work timing')
                if not math.isfinite(row['gpu_us']) or row['gpu_us'] < 0:
                    raise ValueError('Invalid GPU duration')
            target[key] = row
        elif kind in ('calibration', 'stage'):
            stage = integer(row, 'stage', positive=True)
            if stage not in (1, 4, 8):
                raise ValueError('Unknown feedback stage')
            domain = integer(row, 'domain')
            if domain not in (1000208000, 1000208001):
                raise ValueError('Uncalibrated feedback clock domain')
            domain_id = integer(row, 'domain_id')
            if kind == 'calibration':
                integer(row, 'host_qpc', positive=True)
                integer(row, 'local_ns', positive=True)
                if integer(row, 'max_deviation_ns') == 0:
                    # The spec requires a strictly positive bound. Retain the
                    # raw driver record but do not treat zero as perfect accuracy.
                    invalid_calibrations += 1
                    continue
                calibrations[generation, domain, domain_id, stage].append(row)
            else:
                key = generation, integer(row, 'id', positive=True), stage
                if key in stages or row.get('complete') is not True:
                    raise ValueError('Duplicate/incomplete stage result')
                integer(row, 'time')
                stages[key] = row
        else:
            raise ValueError('Unknown feedback record')
    if integer(rows[-1], 'requests') != len(presents):
        raise ValueError('Present accounting mismatch')
    completed = {(g, i) for g, i, s in stages}
    if integer(rows[-1], 'completed') != len(completed):
        raise ValueError('Feedback accounting mismatch')
    if not presents:
        raise ValueError('No identified presents')
    for g, i, stage in stages:
        if (g, i) not in presents or g not in chains or not chains[g]['stages'] & stage:
            raise ValueError('Feedback has no matching Present/requested stage owner')
    for key in work:
        if key not in presents:
            raise ValueError('Work has no Present owner')
    for values in calibrations.values():
        values.sort(key=lambda r: r['local_ns'])
        if any(b['host_qpc'] <= a['host_qpc'] or b['local_ns'] <= a['local_ns']
               for a, b in zip(values, values[1:])):
            raise ValueError('Non-monotonic clock calibration')

    def clock(row):
        key = row['generation'], row['domain'], row['domain_id'], row['stage']
        candidates = calibrations.get(key)
        if not candidates:
            raise ValueError('Missing calibration for this chain/domain/stage')
        index = bisect_left([c['local_ns'] for c in candidates], row['time'])
        nearby = candidates[max(0, index-1):index+1]
        calibration = min(nearby, key=lambda r: abs(r['local_ns']-row['time']))
        distance = row['time']-calibration['local_ns']
        if abs(distance) > 1_100_000_000:
            raise ValueError('Feedback clock calibration is stale')
        # Subtract integers BEFORE floating conversion: a stage clock may use
        # a very large epoch, unrelated to Windows QPC or another present stage.
        return (calibration['host_qpc'] + distance*hz/1e9,
                calibration['max_deviation_ns']/1e6)

    first = min(r['begin_qpc'] for r in presents.values())
    metrics = defaultdict(list)
    counts = Counter(presents=len(presents), feedback_completed=len(completed),
                     zero_deviation_calibrations_ignored=invalid_calibrations)
    samples = []
    for (g, i), present in presents.items():
        if g not in chains:
            raise ValueError('Present has no chain owner')
        if present['begin_qpc'] < first + warmup_ms*hz/1000:
            counts['warmup_excluded'] += 1
            continue
        sample = dict(generation=g, id=i, mode=present['mode'])
        for stage, name in ((1, 'queue_end'), (4, 'first_pixel_out'), (8, 'first_pixel_visible')):
            result = stages.get((g, i, stage))
            if result is None:
                continue
            if not chains[g]['stages'] & stage:
                raise ValueError('Feedback stage was not requested')
            if not result['time']:
                counts[name+'_unavailable'] += 1
                continue
            qpc, uncertainty = clock(result)
            delay = (qpc-present['begin_qpc'])*1000/hz
            if delay < -uncertainty-.0001:
                if stage == 1:
                    # Some drivers expose this optional stage through a
                    # different, slewing host clock. It cannot invalidate an
                    # independently calibrated output stage, or become zero.
                    counts['queue_end_invalid_clock_samples'] += 1
                    continue
                raise ValueError('Feedback event precedes its own Present')
            sample[name+'_ms'] = delay
            sample[name+'_uncertainty_ms'] = uncertainty
            metrics['present_to_'+name+'_ms'].append(delay)
        if (g, i) in work:
            record = work[g, i]
            metrics['acquire_wall_ms'].append((record['acquired_qpc']-record['begin_qpc'])*1000/hz)
            metrics['gpu_work_ms'].append(record['gpu_us']/1000)
        if any(key in sample for key in ('first_pixel_out_ms', 'first_pixel_visible_ms')):
            samples.append(sample)
    if not samples:
        raise ValueError('No observed output feedback after warm-up')
    return dict(schema_version=1, pid=pid, frequency=hz, counts=dict(counts),
                warmup_ms=warmup_ms, metrics={k: distribution(v) for k, v in metrics.items()},
                samples=samples,
                scope='Exact Vulkan Present ID to driver presentation-engine output/visibility stage',
                limitations=['FIRST_PIXEL_OUT is not physical scanout, panel response or photons.',
                             'Zero feedback timestamps are unavailable, never zero latency.',
                             'Impossible optional queue-end samples are excluded and counted; output-stage samples remain strict.',
                             'A WSI micro benchmark is not representative gameplay or input latency.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--warmup-ms', type=float, default=1000)
    args = parser.parse_args()
    report = analyze(args.capture.read_text(encoding='utf8'), args.warmup_ms)
    report.update(source=str(args.capture.resolve()), source_sha256=digest(args.capture))
    write_json_new(args.output, report)
    print(json.dumps(report['metrics'], indent=2))


if __name__ == '__main__':
    main()
