"""Run the verified PS4 runtime's non-interactive, ROM-free mechanism checks."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analysis_paths import private_output
from project import ROOT, digest, write_json_new
from ps4_build_receipt import verify

CHECKS = (
    'abi', 'format', 'heap', 'condition', 'semaphore', 'event-flag', 'thread',
    'thread-affinity', 'libc-memory', 'text', 'rtc', 'libc-calendar', 'time',
    'memory', 'tracked-texture', 'write-tracker', 'mspace', 'display-layout',
    'cxx-guard', 'equeue', 'display-clock', 'pad', 'pcm-voice', 'np-request',
    'np-score', 'np-replay', 'community-round-audit', 'sfo', 'pm4',
    'host-timing', 'host-timing-counts', 'host-timing-default', 'gamepad',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='New directory below collection/.local')
    args = parser.parse_args()
    receipt = verify()
    directory = private_output(args.output or ROOT / '.local/ps4-native-checks' /
                               (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8]))
    directory.mkdir(parents=True, exist_ok=False)
    executable = ROOT / 'out/ps4-native/KetsuiNativeProbe.exe'
    results = []
    for name in CHECKS:
        try:
            result = subprocess.run([str(executable), '--' + name + '-self-test'],
                                    cwd=directory, capture_output=True, timeout=45)
            code, stdout, stderr = result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired as error:
            code, stdout, stderr = None, error.stdout or b'', error.stderr or b''
        for suffix, content in (('stdout.jsonl', stdout), ('stderr.txt', stderr)):
            with (directory / (name + '.' + suffix)).open('xb') as output:
                output.write(content)
        results.append(dict(check=name, exitCode=code, passed=code == 0))
        print(('PASS' if code == 0 else 'FAIL') + ': ' + name, flush=True)
    write_json_new(directory / 'result.json', dict(format=1, backend='ps4', gameExecution=False,
        buildSourceCommit=receipt['sourceCommit'], buildSourceDirty=receipt['sourceDirty'],
        executableSha256=digest(executable), checks=results))
    print(f'{sum(item["passed"] for item in results)}/{len(results)} native checks passed; report: {directory}')
    return 0 if all(item['passed'] for item in results) else 1


if __name__ == '__main__':
    sys.exit(main())
