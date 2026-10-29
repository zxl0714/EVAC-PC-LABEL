"""Join a bounded process-CPU window to PS4 frame and GPU timestamp evidence.

GPU spans include dependencies between the batch timestamps; they are not
exclusive GPU busy time. Run each mode in the same warmed scene and report
absolute costs rather than attributing a noisy single CPU window to NEO.
"""
import argparse
import json
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new


def review(cpu_path, launch_path, game):
    cpu = json.loads(cpu_path.read_text())
    launch = json.loads(launch_path.read_text())
    if (cpu['pid'] != launch['pid'] or
            cpu['executable_sha256'] != launch['executable_sha256']):
        raise ValueError('CPU window and launch identity differ')
    fixture, = [Path(a.split('=', 1)[1]) for a in launch['command']
                if a.startswith('--collection_root=')]
    run, = [p for p in (fixture / 'logs' / game).glob('run-*')
            if p.name.endswith('-' + str(cpu['pid']))]
    snapshot = run / 'config.snapshot.ini'
    if digest(snapshot) != launch['config_sha256']:
        raise ValueError('Launch configuration snapshot differs')
    lo, hi = [cpu['samples'][i]['qpc'] * 1_000_000 / cpu['qpc_frequency']
              for i in (0, -1)]
    frames, batches, modes = [], [], []
    stdout = run / 'stdout.jsonl'
    with stdout.open(encoding='utf-8') as stream:
        for line in stream:
            row = json.loads(line)
            event = row.get('event')
            if event == 'collection_configuration':
                modes.append(row)
            elif (event == 'native_gnm_frame_completed' and
                  lo <= row['completed_clock_us'] <= hi):
                frames.append(row)
            elif (event == 'native_gpu_batch_timestamp' and
                  lo <= row['end_recorded_clock_us'] <= hi):
                if row['query_result'] or row['skipped_slots']:
                    raise ValueError('GPU timestamp window is incomplete')
                batches.append(row)
    if len(modes) != 1 or len(frames) < 100 or len(batches) < 100:
        raise ValueError('Missing configuration or insufficient stable work')
    if modes[0]['vsync']:
        raise ValueError('Expected paired low-latency presentation')
    intervals = sorted(r['completion_interval_us']/1000 for r in frames)
    def quantile(q):
        return intervals[min(len(intervals)-1, int((len(intervals)-1)*q))]
    return dict(neo=modes[0]['neo'], cpu_cores=cpu['equivalent_logical_cores'],
        frames=len(frames), gpu_batches=len(batches),
        gpu_batch_span_us=statistics.mean(r['gpu_span_us'] for r in batches),
        draws_per_frame=statistics.mean(r['draws'] for r in frames),
        interval_ms={str(q): quantile(q) for q in (.5, .95, .99, 1)},
        sources=[dict(path=str(p), sha256=digest(p))
                 for p in (cpu_path, launch_path, snapshot, stdout)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', required=True,
                        choices=('ketsui', 'daioujou', 'esprade', 'feveron'))
    parser.add_argument('--base-cpu', type=Path, required=True)
    parser.add_argument('--base-launch', type=Path, required=True)
    parser.add_argument('--neo-cpu', type=Path, required=True)
    parser.add_argument('--neo-launch', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    base = review(args.base_cpu, args.base_launch, args.game)
    neo = review(args.neo_cpu, args.neo_launch, args.game)
    if base['neo'] or not neo['neo']:
        raise ValueError('Expected ordinary / NEO pair')
    result = dict(game=args.game, ordinary=base, neo=neo,
                  reviewer_sha256=digest(Path(__file__)))
    write_json_new(args.output, result)
    print(json.dumps({mode: {k: v for k, v in row.items() if k != 'sources'}
                      for mode, row in (('ordinary', base), ('neo', neo))}))


if __name__ == '__main__':
    main()
