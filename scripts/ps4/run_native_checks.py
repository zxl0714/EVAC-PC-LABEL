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
    'host-timing', 'host-timing-counts', 'host-timing-default', 'gamepad', 'collection-config', 'collection-input', 'async-log',
)


def configuration_cases():
    return (
        ('defaults', (ROOT / 'config/default.ini').read_text(encoding='utf-8-sig'), 0,
         {'game':'ketsui', 'title':'CAVE PC Collection — ケツイ Deathtiny ～絆地獄たち～',
          'community':{'enabled':True, 'nickname':'Player', 'endpoint':'https://xiaolu.life/cave/api/v1/games/ketsui'}}),
        ('utf8-bom', '\ufeff[COLLECTION]\nconfig_version=1\n[COMMUNITY]\nnickname=弹幕 Player\n', 0,
         {'community':{'nickname':'弹幕 Player'}}),
        ('overrides', '[display]\nfullscreen=true\n[audio]\nvolume=0.8\n[game.ketsui.display]\nfullscreen=false\nvsync=false\n[game.ketsui.audio]\nvolume=0.25\n[game.ketsui.keyboard]\nsouth=Space,B\n', 0,
         {'fullscreen':False, 'vsync':False, 'volume':0.25, 'keyboard':{'south':[32,66]}}),
        ('sibling-isolation', '[game.futari.keyboard]\nunknown=bad\n[game.espgaluda2.gamepad]\ndeadzone=bad\n', 0,
         {'gamepad':{'enabled':True,'deadzone':0.2}}),
        ('legacy-endpoint', '[community]\nendpoint=https://xiaolu.life/ketsui/api/v1/\n', 0,
         {'community':{'endpoint':'https://xiaolu.life/ketsui/api/v1'}}),
        ('game-endpoint', '[community]\nendpoint=https://xiaolu.life/cave/api/v1/games/ketsui/\n', 0,
         {'community':{'endpoint':'https://xiaolu.life/cave/api/v1/games/ketsui'}}),
        ('disabled', '[game.ketsui.gamepad]\nenabled=false\ndeadzone=0.35\n[game.ketsui.community]\nenabled=false\n', 0,
         {'gamepad':{'enabled':False,'deadzone':0.35},'community':{'enabled':False}}),
        ('reserved', '[keyboard]\nsouth=F11\n', 2, {}),
        ('duplicate', '[keyboard]\nsouth=Z\nsouth=B\n', 2, {}),
        ('conflict', '[keyboard]\nsouth=X\n', 2, {}),
        ('nonfinite', '[audio]\nvolume=nan\n', 2, {}),
        ('newer-version', '[collection]\nconfig_version=2\n', 2, {}),
        ('legacy-button', '[keyboard]\ncross=Z\n', 2, {}),
        ('wrong-game-endpoint', '[community]\nendpoint=https://xiaolu.life/cave/api/v1/games/futari\n', 2, {}),
        ('missing-file', None, 2, {}),
    )


def subset(actual, expected):
    return (isinstance(actual, dict) and all(key in actual and subset(actual[key], value) for key,value in expected.items())) \
        if isinstance(expected, dict) else actual == expected


def check_configuration_files(executable, directory):
    results=[]
    fixtures=directory / '配置检查 with spaces'
    fixtures.mkdir()
    for name, content, expected_code, expected in configuration_cases():
        path=fixtures / (name + '.ini')
        if content is not None:
            with path.open('xb') as output:
                output.write(content.encode('utf-8'))
        before=digest(path) if content is not None else None
        result=subprocess.run([str(executable),'--collection-config-check',str(path)],
                              cwd=directory,capture_output=True,timeout=15)
        valid=result.returncode==expected_code
        if expected_code==0:
            try:
                report=json.loads(result.stdout)
                valid=valid and report.get('event')=='collection_configuration' and subset(report, expected)
            except (ValueError, TypeError):
                valid=False
        valid=valid and ((digest(path)==before) if content is not None else not path.exists())
        for suffix, content in (('stdout.jsonl',result.stdout),('stderr.txt',result.stderr)):
            with (directory / ('config-'+name+'.'+suffix)).open('xb') as output:
                output.write(content)
        results.append(dict(check='config-'+name, exitCode=result.returncode, expectedExitCode=expected_code, passed=valid))
        print(('PASS' if valid else 'FAIL')+': config-'+name, flush=True)
    return results


def check_terminal_logs(executable, directory):
    results = []
    for name, expected_code, final_event in (('terminal-log', 20, 'unimplemented_api'),
                                              ('terminal-close-log', 27, 'native_shutdown_incomplete')):
        try:
            result = subprocess.run([str(executable), '--' + name + '-self-test'],
                                    cwd=directory, capture_output=True, timeout=15)
            code, stdout, stderr = result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired as error:
            code, stdout, stderr = None, error.stdout or b'', error.stderr or b''
        valid = code == expected_code
        try:
            events = [json.loads(line) for line in stdout.splitlines()]
            snapshots = [event for event in events if event.get('event') == 'native_async_log']
            last = snapshots[-1]
            valid = (valid and events[-1].get('event') == final_event and
                     not any(event.get('event') == 'late_writer' for event in events) and
                     last.get('stop_requested') is True and last.get('drain_completed') is True and
                     last.get('pending') == 0 and last.get('in_flight') == 0 and last.get('sink_failures') == 0 and
                     last['written'] + last['dropped_oldest'] == last['accepted'])
        except (ValueError, KeyError, TypeError, IndexError, AttributeError):
            valid = False
        for suffix, content in (('stdout.jsonl', stdout), ('stderr.txt', stderr)):
            with (directory / (name + '.' + suffix)).open('xb') as output:
                output.write(content)
        results.append(dict(check=name, exitCode=code, expectedExitCode=expected_code, passed=valid))
        print(('PASS' if valid else 'FAIL') + ': ' + name, flush=True)
    return results


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
    results.extend(check_configuration_files(executable, directory))
    results.extend(check_terminal_logs(executable, directory))
    write_json_new(directory / 'result.json', dict(format=1, backend='ps4', gameExecution=False,
        buildSourceCommit=receipt['sourceCommit'], buildSourceDirty=receipt['sourceDirty'],
        executableSha256=digest(executable), checks=results))
    print(f'{sum(item["passed"] for item in results)}/{len(results)} native checks passed; report: {directory}')
    return 0 if all(item['passed'] for item in results) else 1


if __name__ == '__main__':
    sys.exit(main())
