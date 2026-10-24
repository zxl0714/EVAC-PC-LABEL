"""Run the verified PS4 runtime's non-interactive, ROM-free mechanism checks."""
import argparse
import datetime
import importlib.util
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
    'abi', 'format', 'heap', 'condition', 'semaphore', 'event-flag', 'thread', 'math',
    'thread-affinity', 'libc-memory', 'text', 'rtc', 'libc-calendar', 'time',
    'memory', 'tracked-texture', 'write-tracker', 'mspace', 'display-layout',
    'cxx-guard', 'equeue', 'display-clock', 'pad', 'pcm-voice', 'np-request',
    'np-score', 'np-score-profile', 'np-replay', 'community-round-audit', 'community-client', 'sfo', 'pm4',
    'host-timing', 'host-timing-counts', 'host-timing-default', 'gamepad', 'collection-config', 'collection-input', 'async-log', 'indexed-draw',
)


def configuration_cases():
    return (
        ('defaults', (ROOT / 'config/default.ini').read_text(encoding='utf-8-sig'), 0,
         {'game':'ketsui', 'title':'EVAC PC LABEL — ケツイ Deathtiny ～絆地獄たち～',
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


def check_content_layout(executable, directory):
    # Reuse the project's authored SFO/content and probe fixtures; no ROM input.
    sys.path.insert(0,str(ROOT / 'scripts/ps4'))
    spec=importlib.util.spec_from_file_location('ps4_authored_content',ROOT / 'tests/ketsui/test_native.py')
    fixtures=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixtures)
    scene=directory / 'authored-content'
    scene.mkdir()
    app,patch,addons=fixtures.content_tree(scene)
    probe=scene / 'authored.ktprobe'
    probe.write_bytes(fixtures.exit_fixture())
    pacing_configs={}
    for name, community, vsync in (('offline',False,False),('online',True,False),('vsync',False,True)):
        path=scene / (name+'.ini')
        path.write_text('[community]\nenabled='+str(community).lower()+'\n[display]\nvsync='+str(vsync).lower()+'\n',encoding='utf-8')
        pacing_configs[name]=str(path)
    before={str(path):digest(path) for path in scene.rglob('*') if path.is_file()}
    cases=[('content-layout-self-test',['--content-self-test',str(app),str(patch),str(addons)],0)]
    cases.extend((
        ('background-log-fixture',[str(probe),'--enter','--background-log','on'],7),
        ('background-log-validation-rejected',[str(probe),'--validate','--background-log','on'],2),
        ('background-log-bad-value',[str(probe),'--enter','--background-log','off'],2)))
    diagnostic=[str(probe),'--enter','--diagnostic-unpaced','on']
    user=['--user-dir',str(directory / 'benchmark-authored')]
    cases.extend((
        ('unpaced-offline-fixture',diagnostic+user+['--collection-config',pacing_configs['offline']],7),
        ('unpaced-online-rejected',diagnostic+user+['--collection-config',pacing_configs['online']],2),
        ('unpaced-vsync-rejected',diagnostic+user+['--collection-config',pacing_configs['vsync']],2),
        ('unpaced-normal-profile-rejected',diagnostic+['--user-dir',str(directory / 'default'),
                                                    '--collection-config',pacing_configs['offline']],2),
        ('unpaced-missing-config-rejected',diagnostic+user,2),
        ('unpaced-validation-rejected',[str(probe),'--validate','--diagnostic-unpaced','on',
                                        '--collection-config',pacing_configs['offline'],*user],2),
        ('unpaced-bad-value',[str(probe),'--enter','--diagnostic-unpaced','off',
                              '--collection-config',pacing_configs['offline'],*user],2)))
    prefix=[str(probe),'--validate']
    for name,options,expected in (
        ('merged-explicit',['--app-dir',str(patch),'--merged-app','on'],0),
        ('merged-not-implicit',['--app-dir',str(patch)],2),
        ('merged-not-base',['--app-dir',str(app),'--merged-app','on'],2),
        ('merged-no-second-patch',['--app-dir',str(patch),'--patch-dir',str(patch),'--merged-app','on'],2),
        ('merged-bad-value',['--app-dir',str(patch),'--merged-app','off'],2),
        ('merged-needs-content',['--merged-app','on'],2)):
        cases.append((name,prefix+options,expected))
    results=[]
    for name,arguments,expected in cases:
        result=subprocess.run([str(executable),*arguments],cwd=directory,capture_output=True,timeout=15)
        passed=result.returncode==expected and before=={str(path):digest(path) for path in scene.rglob('*') if path.is_file()}
        if name=='content-layout-self-test' and passed:
            events=[json.loads(line) for line in result.stdout.splitlines()]
            checks=[event for event in events if event['event']=='content_self_test_case']
            passed=len(checks)==23 and all(event['passed'] for event in checks)
        if name=='background-log-fixture' and passed:
            events=[json.loads(line) for line in result.stdout.splitlines()]
            stats=next(event for event in reversed(events) if event['event']=='native_async_log')
            passed=events[-1]['event']=='native_exit' and stats['drain_completed'] and stats['pending']==stats['in_flight']==0
            pacing=next(event for event in events if event['event']=='native_presentation_pacing')
            passed &= not pacing['diagnostic_unpaced'] and pacing['software_flip_wait']
        if name=='unpaced-offline-fixture' and passed:
            events=[json.loads(line) for line in result.stdout.splitlines()]
            pacing=next(event for event in events if event['event']=='native_presentation_pacing')
            passed=(pacing['diagnostic_unpaced'] and not pacing['software_flip_wait'] and
                    pacing['guest_vblank_clock_preserved'] and pacing['gpu_fences_preserved'])
        for suffix,content in (('stdout.jsonl',result.stdout),('stderr.txt',result.stderr)):
            with (directory / (name+'.'+suffix)).open('xb') as output:output.write(content)
        results.append(dict(check=name,exitCode=result.returncode,expectedExitCode=expected,passed=passed))
        print(('PASS' if passed else 'FAIL')+': '+name,flush=True)
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
            command=[str(executable), '--' + name + '-self-test']
            if name=='community-client':
                command.append(str(directory/'community-client-fixture'))
            if name=='indexed-draw':
                command.append(str(ROOT / 'out/ps4-gpu/KetsuiGpuRuntime.dll'))
            result = subprocess.run(command,
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
    results.extend(check_content_layout(executable, directory))
    write_json_new(directory / 'result.json', dict(format=1, backend='ps4', gameExecution=False,
        buildSourceCommit=receipt['sourceCommit'], buildSourceDirty=receipt['sourceDirty'],
        executableSha256=digest(executable), checks=results))
    print(f'{sum(item["passed"] for item in results)}/{len(results)} native checks passed; report: {directory}')
    return 0 if all(item['passed'] for item in results) else 1


if __name__ == '__main__':
    sys.exit(main())
