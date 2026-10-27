"""Summarize the developer trace benchmark, keeping replay setup separate."""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics

GPU = re.compile(
    r"GPU_WORK submission=(\d+) frame=(\d+) gpu_us=([\d.]+) encode_us=([\d.]+) "
    r"draws=(\d+) copies=(\d+) closes_frame=(true|false) dropped=(\d+) invalid=(\d+)"
)


def distribution(values: list[float]) -> dict:
    if not values or any(not math.isfinite(v) or v < 0 for v in values):
        raise ValueError("Missing or invalid samples")
    ordered = sorted(values)
    return {"count": len(values), "mean": statistics.mean(values),
            "median": statistics.median(values),
            "p95": ordered[min(len(ordered) - 1, int(len(ordered) * .95))],
            "max": max(values)}


def paired_overwrite(rows, measured, complete, work):
    indexed = {int(r['iteration']): r for r in measured}
    pairs = []
    for start in range(0, len(rows), 4):
        ids = list(range(start, start + 4))
        if not all(i in indexed and i + 1 in complete for i in ids):
            continue
        if [indexed[i].get('overwrite_upload') for i in ids] != ['0', '1', '1', '0']:
            raise ValueError('Invalid overwrite ABBA order')
        pairs.append((ids, [indexed[i] for i in ids]))
    if not pairs:
        raise ValueError('No complete overwrite groups')
    result = {}
    for name in ('packets_wall_us', 'gpu_work_us'):
        old, new = [], []
        for ids, group in pairs:
            values = ([float(r['packets_us']) for r in group] if name == 'packets_wall_us'
                      else [work[i + 1] for i in ids])
            old.append((values[0] + values[3]) / 2)
            new.append((values[1] + values[2]) / 2)
        if not all(v > 0 for v in old):
            raise ValueError('Zero overwrite baseline')
        result[name] = dict(groups=len(old), before=distribution(old), after=distribution(new),
            mean_reduction_percent=100 * (1 - statistics.mean(new) / statistics.mean(old)),
            median_group_reduction_percent=statistics.median(100 * (1 - b/a) for a,b in zip(old,new)),
            improved_groups=sum(b < a for a,b in zip(old,new)))
    return result


def summarize(root: Path) -> dict:
    with (root / "replays.csv").open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows or any(int(row["iteration"]) != n for n, row in enumerate(rows)):
        raise ValueError("Incomplete replay sequence")
    if len({(r["rgb_hash"], r["width"], r["height"]) for r in rows}) != 1:
        raise ValueError("Replay pixels changed")
    measured = [r for r in rows if r["phase"] == "measured"]
    if not measured or [r['phase'] for r in rows] != (
            ['warmup'] * (len(rows) - len(measured)) + ['measured'] * len(measured)):
        raise ValueError("Invalid benchmark phases")
    frames = {int(r["iteration"]) + 1 for r in measured}
    parsed = [match.groups() for match in GPU.finditer((root / "gpu.log").read_text(encoding="utf-8"))]
    if any(int(r[7]) or int(r[8]) for r in parsed):
        raise ValueError("GPU timestamps were dropped or invalid")
    if any(int(b[0]) != int(a[0]) + 1 for a, b in zip(parsed, parsed[1:])):
        raise ValueError("Incomplete GPU submission sequence")
    # Collection happens on the next submission. The last frame may not yet
    # have been collected when the benchmark exits; never fill it with zero.
    complete = {int(r[1]) for r in parsed if r[6] == "true"} & frames
    if not complete or frames - complete not in (set(), {max(frames)}):
        raise ValueError("Missing GPU frame samples")
    setup: dict[int, float] = {n: 0 for n in complete}
    work: dict[int, float] = {n: 0 for n in complete}
    for record in parsed:
        frame = int(record[1])
        if frame not in complete:
            continue
        (work if int(record[4]) or int(record[5]) else setup)[frame] += float(record[2])
    settings_path = root / 'settings.json'
    settings = json.loads(settings_path.read_text(encoding='utf-8')) if settings_path.exists() else None
    variants = {}
    paired = None
    if 'native_rgba8' in rows[0]:
        if any(r['native_rgba8'] not in ('0', '1') for r in rows):
            raise ValueError('Invalid resolve variant')
        for value in sorted({r['native_rgba8'] for r in measured}):
            variant_rows = [r for r in measured if r['native_rgba8'] == value]
            ids = {int(r['iteration']) + 1 for r in variant_rows} & complete
            variants[value] = dict(
                gpu_us=distribution([work[n] for n in ids]),
                packets_us=distribution([float(r['packets_us']) for r in variant_rows]))
        if settings and settings.get('alternate_resolve'):
            indexed = {int(r['iteration']): r for r in measured}
            old, new = [], []
            for start in range(0, len(rows), 4):
                indices = range(start, start + 4)
                if not all(i in indexed and i + 1 in complete for i in indices):
                    continue
                if [indexed[i]['native_rgba8'] for i in indices] != ['0', '1', '1', '0']:
                    raise ValueError('Invalid interleaved ABBA order')
                old.append((work[start + 1] + work[start + 4]) / 2)
                new.append((work[start + 2] + work[start + 3]) / 2)
            if not old or not all(v > 0 for v in old):
                raise ValueError('No complete interleaved groups')
            paired = dict(groups=len(old), old_gpu_us=distribution(old), new_gpu_us=distribution(new),
                          reduction_percent=(1 - statistics.mean(new) / statistics.mean(old)) * 100,
                          median_group_reduction_percent=statistics.median(
                              [(1 - b / a) * 100 for a, b in zip(old, new)]),
                          improved_groups=sum(b < a for a, b in zip(old, new)))
    return {
        "directory": str(root), "rgb_hash": rows[0]["rgb_hash"],
        "dimensions": [int(rows[0]["width"]), int(rows[0]["height"])],
        "settings": settings,
        "resolve_variants": variants,
        "interleaved": paired,
        "overwrite_interleaved": paired_overwrite(rows, measured, complete, work)
            if settings and settings.get('alternate_overwrite') else None,
        "cpu_us": {key: distribution([float(r[key]) for r in measured])
                   for key in ("restore_us", "packets_us", "primary_end_us", "capture_wall_us")},
        "gpu_us": {"snapshot_submission": distribution(list(setup.values())),
                   "guest_work_submissions": distribution(list(work.values()))},
        "restored_bytes_per_iteration": distribution([int(r["restored_bytes"]) for r in measured]),
        "reused_bytes_per_iteration": distribution([int(r.get("reused_bytes", 0)) for r in measured]),
        "scope": "Offline same-capture replay. Guest submissions include texture reloads; "
                 "not steady gameplay or scanout. Original image equality is a separate gate.",
    }


def compare(roots: list[Path]) -> list[dict]:
    reports = [summarize(root) for root in roots]
    if len({(r["rgb_hash"], tuple(r["dimensions"])) for r in reports}) != 1:
        raise ValueError("Comparison inputs have different output images")
    settings = [r['settings'] for r in reports]
    if any(settings):
        if not all(settings):
            raise ValueError("Comparison settings missing")
        keys = ('trace_xxh3', 'trace_bytes', 'reuse_memory', 'copy_repetitions', 'alternate_resolve',
                'alternate_overwrite', 'profile_cpu_work')
        if len({tuple(s.get(k, False) for k in keys) for s in settings}) != 1:
            raise ValueError("Comparison inputs or replay settings differ")
    return reports


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", type=Path, nargs="+")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    reports = compare(args.directories)
    text = json.dumps(reports, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        with args.output.open("x", encoding="utf-8") as stream:
            stream.write(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
