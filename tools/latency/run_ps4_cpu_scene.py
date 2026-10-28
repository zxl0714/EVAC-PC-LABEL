"""Replay an accepted private navigation trace, then sample a bounded CPU scene.

Uses only the explicitly enabled in-process test-pad API, never Windows input.
The resulting numbers still require a visual scene check before acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from latency_probe import read_trace
from project import write_json_new


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--launch', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--scene', required=True)
    parser.add_argument('--seconds', type=int, choices=range(2, 11), default=4)
    args = parser.parse_args()
    base = ROOT / '.local/input-latency-v080-r1'
    reference, launch, output = (p.resolve() for p in (args.reference, args.launch, args.output))
    if not all(p.is_relative_to(base) for p in (reference, launch, output)) or output.exists():
        raise ValueError('Existing private inputs and fresh private output required')
    metadata = output.with_name(output.stem + '-navigation.json')
    if metadata.exists():
        raise ValueError('Fresh navigation receipt required')
    receipt = json.loads(launch.read_text())
    pipe = receipt['environment']['EVAC_LATENCY_TEST_PAD']
    trace = read_trace(reference.read_text())
    if not trace['complete'] or trace['overflow']:
        raise ValueError('Complete original navigation trace required')
    rows = trace['records']
    # Command90 arms ROI capture in the private pixel observer. Keep it OFF
    # throughout CPU sampling; the prior navigation pulse has already released.
    navigation = [r for r in rows if r['kind'] == 14 and r['c'] == 1 and 0 < r['a'] < 90]
    anchors = [r for r in rows if r['kind'] == 14 and r['c'] == 1 and r['a'] == 101]
    if not navigation or len(navigation) > 50 or not anchors:
        raise ValueError('Bounded original navigation and first scene pulse required')
    origin, anchor = navigation[0]['qpc'], anchors[0]['qpc']
    if not 0 < (anchor-origin)/trace['frequency'] <= 180:
        raise ValueError('Navigation duration outside the accepted bound')
    executable = Path(receipt['command'][0])
    if hashlib.sha256(executable.read_bytes()).hexdigest() != receipt['executable_sha256']:
        raise ValueError('Launched executable identity changed')
    client = [sys.executable, str(ROOT/'tools/latency/test_pad_client.py'), '--pipe', pipe]
    started = time.perf_counter()
    def until(qpc):
        deadline = started + (qpc-origin)/trace['frequency']
        while time.perf_counter() < deadline:
            time.sleep(max(0, min(1, deadline-time.perf_counter())))
    sent = []
    for row in navigation:
        releases = [r for r in rows if r['kind']==14 and r['a']==row['a'] and
                    r['c']==0 and r['qpc'] >= row['qpc']]
        if not releases or not 0 <= row['b'] <= 65535:
            raise ValueError('Original pulse has no bounded release')
        hold = max(1, round((releases[0]['qpc']-row['qpc'])*1000/trace['frequency']))
        if hold > 2000:
            raise ValueError('Original pulse is too long')
        until(row['qpc'])
        subprocess.run(client + ['--id', str(row['a']), '--hold-ms', str(hold),
                                '--buttons', hex(row['b'])], check=True)
        sent.append(dict(command=row['a'], buttons=row['b'], hold_ms=hold,
                         elapsed_seconds=time.perf_counter()-started))
    until(anchor)
    pulses = subprocess.Popen(client + ['--id','1001','--hold-ms','100','--buttons','x,left',
                                       '--alternate','x,right','--repeat',str(args.seconds*5),
                                       '--gap-ms','97'])
    try:
        subprocess.run([sys.executable, str(ROOT/'tools/latency/sample_process_cpu.py'),
                        '--pid', str(receipt['pid']), '--seconds', str(args.seconds),
                        '--scene', args.scene, '--output', str(output)], check=True)
    finally:
        pulses.wait(timeout=3)
    # Preserve the end-of-sample scene for inspection despite model/tool delay.
    # This pulse is after the sampler's final process-time read, never part of
    # the measured workload. M2 uses touchpad (test-pad Back) for its pause menu;
    # Options is the arcade Start button and does not pause gameplay.
    subprocess.run(client + ['--id','90','--hold-ms','1','--buttons','0x0'], check=True)
    time.sleep(.1)
    subprocess.run(client + ['--id','91','--hold-ms','180','--buttons','back'], check=True)
    # Only retain the bounded end-of-sample scene, not later idle gameplay.
    time.sleep(.15)
    subprocess.run(client + ['--id','65000','--hold-ms','1','--buttons','0x0'], check=True)
    write_json_new(metadata, dict(reference=str(reference),
        reference_sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
        launch=str(launch), launch_sha256=hashlib.sha256(launch.read_bytes()).hexdigest(),
        steps=sent, reference_scene_seconds=(anchor-origin)/trace['frequency'],
        sample_seconds=args.seconds, pause_requested_after_sample=True, scene_verified=False,
        scope='Same recorded menu timing; no assumption that a CPU result proves gameplay'))


if __name__ == '__main__':
    main()
