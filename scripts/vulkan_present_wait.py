"""Validate identified asynchronous Vulkan presentation-completion bounds."""
import argparse
import json
import math
from pathlib import Path
import statistics

from project import digest, write_json_new


def analyze(rows, warmup=60):
    if len(rows) < 3 or rows[0].get('kind') != 'header' or rows[-1].get('kind') != 'end':
        raise ValueError('Complete present-wait capture required')
    header, footer = rows[0], rows[-1]
    if (not isinstance(header.get('frequency'), int) or header['frequency'] <= 0 or
            not header.get('pid') or header.get('completion_upper_bound') is not True):
        raise ValueError('Missing process/clock/endpoint identity')
    frames = rows[1:-1]
    if (footer.get('dropped') != 0 or len(frames) != footer.get('completed') or
            len(frames) != footer.get('requests') or not 0 <= warmup < len(frames)):
        raise ValueError('Incomplete/overflowing observation or empty steady sample')
    sources = set()
    for expected, frame in enumerate(frames, 1):
        if frame.get('kind') != 'wait' or frame.get('id') != expected or frame.get('result') != 0:
            raise ValueError('Missing/replaced present ID or unsuccessful completion')
        if (not all(isinstance(frame.get(k), int) for k in ('source', 'begin', 'end', 'observed')) or
                frame['source'] in sources or not 0 < frame['begin'] <= frame['end'] <= frame['observed']):
            raise ValueError('Duplicate source or non-causal completion')
        sources.add(frame['source'])
    factor = 1000/header['frequency']
    values = sorted((r['observed']-r['begin'])*factor for r in frames[warmup:])
    metric = dict(samples=len(values), mean_ms=statistics.mean(values),
                  p50_ms=statistics.median(values), min_ms=values[0], max_ms=values[-1],
                  p95_ms=values[math.ceil(len(values)*.95)-1] if len(values) >= 20 else None)
    return dict(pid=header['pid'], requests=len(frames), warmup=warmup,
        present_to_completion_bound_ms=metric,
        scope='Exact present ID; asynchronous completion-return upper bound, not an OS adoption timestamp or panel photons')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--warmup', type=int, default=60)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = analyze([json.loads(line) for line in args.capture.read_text().splitlines()], args.warmup)
    result.update(source=str(args.capture.resolve()), source_sha256=digest(args.capture))
    write_json_new(args.output, result)
    print(json.dumps(result, indent=2))
