"""ROM-free, window-free benchmarks of the maintained PS4 command/GPU and semaphore paths."""
import argparse
from functools import lru_cache
import json
import math
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analysis_paths import private_output
from project import ROOT, digest, reject_link, write_json_new
from ps4_build_receipt import verify

SUITE = 'ps4-authored-offline-v1'
GPU_CASES = {'pm4-170-60': (170, 60), 'pm4-250-86': (250, 86)}
HANDOFF_CASES = {f'handoff-{tasks}-{cadence}': (tasks, cadence == 'cadenced')
                 for tasks in (14, 16) for cadence in ('burst', 'cadenced')}
GPU_FIELDS = ('snapshot_us', 'execute_us', 'draw_us', 'acquire_us', 'prefetch_us',
              'flush_us', 'other_us', 'worker_handoff_us', 'completion_us',
              'verification_us', 'total_us')
PARTS = ('draw_us', 'acquire_us', 'prefetch_us', 'flush_us', 'other_us', 'worker_handoff_us')


def natural(value):
    return type(value) is int and 0 <= value <= (1 << 64) - 1


@lru_cache(maxsize=32)
def task_checksum(count):
    mask = (1 << 64) - 1
    checksum = 0
    for ticket in range(count):
        value = ticket + 1
        for _ in range(32):
            value = ((value ^ (value >> 7)) * 0x9e3779b97f4a7c15) & mask
        checksum = (checksum + value) & mask
    return checksum


def parse_run(text, samples, warmup):
    if type(samples) is not int or type(warmup) is not int or not 1 <= samples <= 10000 or not 0 <= warmup <= 1000:
        raise ValueError('Invalid benchmark evidence counts')
    cases, rows, begin, complete, diagnostics, device = {}, {}, None, None, None, None
    for line in text.splitlines():
        if not line.startswith('{'):
            continue
        item = json.loads(line)
        event = item.get('event')
        if event == 'gpu_runtime_device':
            if device is not None:
                raise ValueError('Benchmark unexpectedly created multiple GPU contexts')
            device = {field: item.get(field) for field in ('vendor', 'device_id', 'api_version', 'driver_version')}
            if any(not natural(value) or value > 0xffffffff for value in device.values()):
                raise ValueError('Invalid benchmark GPU device identity')
        if event == 'native_async_log' and item.get('stop_requested') is True:
            diagnostics = item
        if event == 'offline_bench_begin':
            if begin is not None:
                raise ValueError('Duplicate benchmark identity')
            if item.get('semaphore_wait') not in ('cv', 'address', 'event'):
                raise ValueError('Missing or invalid semaphore wait primitive')
            begin = item
        elif event == 'offline_bench_case':
            name = item.get('name')
            if name not in GPU_CASES.keys() | HANDOFF_CASES.keys() or name in cases:
                raise ValueError('Unknown or duplicate benchmark case')
            cases[name] = item
            rows[name] = []
        elif event == 'offline_bench_sample':
            name = item.get('name')
            if name not in rows or type(item.get('index')) is not int or item['index'] != len(rows[name]):
                raise ValueError('Benchmark samples missing, duplicated or out of order')
            rows[name].append(item)
        elif event == 'offline_bench_complete':
            if complete is not None:
                raise ValueError('Duplicate benchmark completion')
            complete = item
    if not begin or type(begin.get('schema')) is not int or begin['schema'] != 1 or \
            type(begin.get('samples')) is not int or begin['samples'] != samples or \
            type(begin.get('warmup')) is not int or begin['warmup'] != warmup or \
            begin.get('game_execution') is not False or begin.get('window_created') is not False or \
            begin.get('authored_only') is not True or begin.get('validation') is not False:
        raise ValueError('Missing or incorrect authored benchmark identity')
    if device is None:
        raise ValueError('Actual GPU/driver identity was not recorded')
    if set(cases) != GPU_CASES.keys() | HANDOFF_CASES.keys() or not complete or \
            type(complete.get('schema')) is not int or type(complete.get('cases')) is not int or \
            complete != dict(event='offline_bench_complete', schema=1, passed=True, cases=6) or complete.get('passed') is not True:
        raise ValueError('Benchmark did not complete every declared case')
    if not diagnostics or diagnostics.get('drain_completed') is not True or any(diagnostics.get(name) != 0 for name in
            ('dropped_oldest', 'invalid_records', 'format_failures', 'sink_failures', 'pending', 'in_flight')):
        raise ValueError('Benchmark diagnostics did not drain cleanly')
    for name, case in cases.items():
        if type(case.get('samples')) is not int or case['samples'] != samples or \
                type(case.get('warmup')) is not int or case['warmup'] != warmup or len(rows[name]) != samples:
            raise ValueError('Benchmark sample/warmup count differs')
        if name in GPU_CASES:
            draws, prefetches = GPU_CASES[name]
            expected = dict(kind='pm4_gpu', draws=draws, prefetches=prefetches, acquires=1,
                            acquire_after_draws=8, shader_variants=2, verified_frames=samples + warmup)
            if any(type(case.get(key)) is not type(value) or case[key] != value for key, value in expected.items()) or not natural(case.get('first_iteration_us')):
                raise ValueError('GPU benchmark workload/correctness contract differs')
            for row in rows[name]:
                if any(not natural(row.get(field)) for field in GPU_FIELDS) or \
                        sum(row[field] for field in PARTS) != row['execute_us'] or \
                        row['snapshot_us'] + row['execute_us'] + row['completion_us'] + row['verification_us'] > row['total_us']:
                    raise ValueError('GPU benchmark timings do not reconcile')
        else:
            tasks, paced = HANDOFF_CASES[name]
            expected_tasks = (samples + warmup) * tasks
            if case.get('kind') != 'semaphore' or type(case.get('workers')) is not int or case['workers'] != 6 or \
                    type(case.get('tasks_per_batch')) is not int or case['tasks_per_batch'] != tasks or case.get('cadenced') is not paced or \
                    not natural(case.get('verified_tasks')) or case['verified_tasks'] != expected_tasks or \
                    not natural(case.get('verified_checksum')) or case['verified_checksum'] != task_checksum(expected_tasks):
                raise ValueError('Semaphore task count/checksum contract differs')
            masks = case.get('native_masks')
            if not isinstance(masks, list) or len(masks) != 6 or any(not natural(value) or not value for value in masks):
                raise ValueError('Semaphore worker mapping is missing')
            if any(not natural(row.get(field)) for row in rows[name] for field in ('handoff_us', 'cadence_wait_us')):
                raise ValueError('Invalid semaphore timing sample')
    return dict(identity=begin, device=device, cases=cases, rows=rows, diagnostics=diagnostics)


def distribution(values):
    values = sorted(values)
    if not values:
        raise ValueError('Cannot summarize an empty benchmark')
    return dict(count=len(values), mean_us=sum(values) / len(values),
                median_us=values[math.ceil(len(values) * .5) - 1],
                p95_us=values[math.ceil(len(values) * .95) - 1],
                p99_us=values[math.ceil(len(values) * .99) - 1], max_us=values[-1])


def summarize(runs):
    result = {}
    for name in GPU_CASES.keys() | HANDOFF_CASES.keys():
        fields = GPU_FIELDS if name in GPU_CASES else ('handoff_us', 'cadence_wait_us')
        result[name] = {field: distribution([row[field] for run in runs for row in run['rows'][name]]) for field in fields}
    return result


def compare(current, previous):
    if previous.get('suite') != current['suite'] or previous.get('parameters') != current['parameters'] or \
            previous.get('host') != current['host'] or previous.get('gpuApi') != current['gpuApi'] or \
            previous.get('device') != current['device']:
        raise ValueError('Comparison needs the same suite, parameters, host, GPU/driver identity and API')
    result = {}
    for name in sorted(current['summary']):
        result[name] = {}
        for field, value in current['summary'][name].items():
            old = previous['summary'][name][field]
            result[name][field] = {stat: dict(before_us=old[stat], after_us=value[stat],
                change_percent=100 * (value[stat] / old[stat] - 1) if old[stat] else None)
                for stat in ('mean_us', 'median_us', 'p95_us', 'p99_us', 'max_us')}
    return result


def execute(output, samples, warmup, repeats, baseline=None, semaphore_wait='address'):
    if semaphore_wait not in ('cv', 'address', 'event'):
        raise ValueError('Unknown semaphore wait primitive')
    receipt = verify()
    directory = private_output(output)
    directory.mkdir(parents=True)
    runtime = directory / 'runtime'
    runtime.mkdir()
    library = runtime / 'KetsuiGpuRuntime.dll'
    shutil.copyfile(ROOT / 'out/ps4-gpu/KetsuiGpuRuntime.dll', library)
    executable = ROOT / 'out/ps4-native/KetsuiNativeProbe.exe'
    binaries = dict(executable=digest(executable), gpu=digest(library))
    runs, artifacts = [], []
    began = time.monotonic()
    for index in range(repeats):
        stdout, stderr = directory / f'run-{index + 1}.jsonl', directory / f'run-{index + 1}.stderr.txt'
        run_began = time.monotonic()
        with stdout.open('xb') as out, stderr.open('xb') as err:
            process = subprocess.run([str(executable), '--offline-bench', str(library), str(samples), str(warmup), semaphore_wait],
                                     cwd=directory, stdout=out, stderr=err, timeout=660)
        if process.returncode:
            raise RuntimeError(f'Offline benchmark failed with exit {process.returncode}; preserve {directory.name}')
        runs.append(parse_run(stdout.read_text(encoding='utf-8'), samples, warmup))
        if runs[-1]['identity']['semaphore_wait'] != semaphore_wait:
            raise ValueError('Executed semaphore wait primitive differs from request')
        artifacts.append(dict(stdout=stdout.name, stdoutSha256=digest(stdout), stderr=stderr.name,
                              stderrSha256=digest(stderr), elapsed_seconds=time.monotonic() - run_began))
    if any(run['identity'] != runs[0]['identity'] or run['device'] != runs[0]['device'] or
           run['cases'] != runs[0]['cases'] for run in runs[1:]):
        # first_iteration_us is a measured value, not a workload input.
        stable_cases = [{name: {key: value for key, value in case.items() if key != 'first_iteration_us'}
                         for name, case in run['cases'].items()} for run in runs]
        if any(run['identity'] != runs[0]['identity'] or run['device'] != runs[0]['device'] for run in runs[1:]) or \
                any(case != stable_cases[0] for case in stable_cases[1:]):
            raise ValueError('Benchmark workload, affinity, process priority or GPU identity changed between repetitions')
    if digest(executable) != binaries['executable'] or digest(library) != binaries['gpu'] or verify() != receipt:
        raise ValueError('Sources or paired benchmark binaries changed during measurement')
    report = dict(schema=1, suite=SUITE, parameters=dict(samples=samples, warmup=warmup, repeats=repeats),
                  host=dict(system=platform.system(), release=platform.release(), version=platform.version(),
                            machine=platform.machine(), processor=platform.processor(), logical_processors=os.cpu_count()),
                  sourceCommit=receipt['sourceCommit'], sourceDirty=receipt['sourceDirty'], semaphoreWait=semaphore_wait,
                  sourceSnapshotSha256=receipt['sourceSnapshot']['sha256'], binaries=binaries,
                  gpuApi=runs[0]['identity']['gpu_api'], device=runs[0]['device'], runIdentity=[run['identity'] for run in runs],
                  workload={name: runs[0]['cases'][name] for name in sorted(runs[0]['cases'])},
                  summary=summarize(runs), perRunSummary=[summarize([run]) for run in runs], artifacts=artifacts,
                  elapsed_seconds=time.monotonic() - began,
                  measurement=[
                      'Authored PM4/GCN uses the production ExecuteNativeGpuCommands, resident Vulkan renderer, fences, drain callbacks and native memory tracking.',
                      'Every GPU iteration verifies one exact alternating red/green pixel; verification/readback is separate from execute and completion.',
                      'Semaphores use six host workers, production semaphore functions/affinity mapping, 14/16-credit batches and a deterministic checked task checksum.',
                      'Cadenced handoffs use the production 60 Hz clock to park workers; cadence_wait_us is intentional and excluded from handoff_us.',
                      'Warmup is excluded. Percentiles are exact nearest-rank samples, not timing-log histogram upper bounds.',
                      'Optional Vulkan validation instrumentation is disabled for benchmark timings, with exact RGB and completion/counter checks retained; game defaults are unchanged.',
                      'Host wall time is not pure CPU/GPU execution. Completion includes real GPU and callback waits; no work or synchronization is omitted.',
                      'No game is loaded and no window is created. Authored point shaders are simpler than game shaders/textures; counts are representative, not a captured game frame.',
                      'No display acquire/composite/present/scanout or guest logic is measured. This does not replace final game replay and pacing checks.',
                      'No uncapped game experiment, networking, save/profile changes, system power changes or process-priority changes are used.',
                      'Raw diagnostics can be contended, but timing rows are written after their bounded logger drains; every declared sample is checked.',
                  ])
    if baseline:
        reject_link(baseline)
        if baseline.stat().st_size > 4 * 1024 * 1024:
            raise ValueError('Benchmark baseline is too large')
        report['comparison'] = compare(report, json.loads(baseline.read_text(encoding='utf-8')))
        report['comparisonBaselineSha256'] = digest(baseline)
    write_json_new(directory / 'report.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='New directory under collection/.local')
    parser.add_argument('--samples', type=int, default=128)
    parser.add_argument('--warmup', type=int, default=16)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--compare', type=Path, help='Previous report.json from identical benchmark parameters/host')
    parser.add_argument('--semaphore-wait', choices=('cv', 'address', 'event'), default='address', help='Same semaphore rules, different host wait primitive')
    args = parser.parse_args()
    if not 1 <= args.samples <= 10000 or not 0 <= args.warmup <= 1000 or not 1 <= args.repeats <= 10:
        parser.error('samples=1..10000, warmup=0..1000, repeats=1..10')
    report = execute(args.output, args.samples, args.warmup, args.repeats, args.compare, args.semaphore_wait)
    print(json.dumps(dict(report=str(args.output / 'report.json'), elapsed_seconds=report['elapsed_seconds'],
                          summary={name: values['execute_us' if name in GPU_CASES else 'handoff_us']
                                   for name, values in report['summary'].items()}), indent=2))


if __name__ == '__main__':
    main()
