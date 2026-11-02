"""Match PS2 frame timing to an exact read-only process CPU sample window."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frames', type=Path, required=True)
    parser.add_argument('--cpu', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    cpu = json.loads(args.cpu.read_text(encoding='utf-8'))
    frequency = cpu['qpc_frequency']
    begin, end = (cpu['samples'][i]['qpc'] * 1e6 / frequency for i in (0, -1))
    with args.frames.open(newline='', encoding='utf-8') as file:
        rows = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(file)]
    if (not rows or min(row['present_qpc_us'] for row in rows) > begin or
            max(row['present_qpc_us'] for row in rows) < end):
        raise ValueError('Frame trace does not cover the whole CPU window')
    selected = [row for row in rows if row['interval_us'] > 0 and
                begin <= row['present_qpc_us'] - row['interval_us'] and row['present_qpc_us'] <= end]
    if len(selected) < 100:
        raise ValueError('Too few complete fields in the CPU window')
    intervals = sorted(row['interval_us'] / 1000 for row in selected)
    record = dict(pid=cpu['pid'], binary_sha256=cpu['executable_sha256'],
        cores=cpu['equivalent_logical_cores'], frames=len(selected),
        median_ms=statistics.median(intervals),
        p95_ms=intervals[math.ceil(.95*len(intervals))-1],
        p99_ms=intervals[math.ceil(.99*len(intervals))-1], max_ms=max(intervals),
        over19=sum(value > 19 for value in intervals), over20=sum(value > 20 for value in intervals),
        longest=sorted(selected, key=lambda row: row['interval_us'], reverse=True)[:5],
        frame_trace_sha256=digest(args.frames), cpu_sample_sha256=digest(args.cpu),
        scene=cpu['scene'])
    write_json_new(args.output, record)
    print(json.dumps({key: value for key, value in record.items() if key != 'longest'}, indent=2))


if __name__ == '__main__':
    main()
