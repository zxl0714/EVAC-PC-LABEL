"""Compare native read-overlay primitives to the Python oracle on authored data."""
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
sys.path.insert(0, str(ROOT / 'tests'))
import ps4_resource_fixtures as fixtures


def path_checks(executable, directory):
    checks = []
    for name, expected_error in [('existing', 'already exists'), ('wrong-place', 'must be under'),
        ('source-overlap', 'overlaps an ELF source'), ('junction', 'cannot traverse'),
        ('alternate-stream', 'alternate data stream'), ('other-game-overlap', 'configured game data'),
        ('relative-component', 'relative components'), ('wrong-extension', '.ktfix'),
        ('duplicate-option', 'repeated resource preparation'), ('empty-option', 'Missing resource'),
        ('invalid-sibling', 'verified CUSA09062'), ('unsupported-executable', 'verified CUSA09062')]:
        root = directory / ('collection-' + name)
        root.mkdir()
        data = root / 'data'
        source = data / 'CUSA09062-patch/eboot.bin'
        source.parent.mkdir(parents=True)
        source.write_bytes(b'authored unsupported executable')
        output = root / '.local/output.ktfix'
        extra = []
        sentinel = b'authored output sentinel'
        if name == 'existing':
            output.parent.mkdir(); output.write_bytes(sentinel)
        elif name == 'wrong-place': output = root / 'user-data/output.ktfix'
        elif name == 'source-overlap':
            data = root / '.local/data'
            source = data / 'CUSA09062-patch/eboot.bin'
            source.parent.mkdir(parents=True); source.write_bytes(b'authored unsupported executable')
            output = data / 'output.ktfix'
        elif name == 'junction':
            (root / '.local').mkdir()
            target = directory / 'junction-target'; target.mkdir()
            link = root / '.local/link'
            created = subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(target)], capture_output=True)
            if created.returncode: raise RuntimeError('Cannot create authored junction fixture')
            output = link / 'output.ktfix'
        elif name == 'alternate-stream': output = root / '.local/output.ktfix:stream'
        elif name == 'other-game-overlap':
            (root / 'config.ini').write_text('[game.futari]\ndata_dir=.local/blocked\n', encoding='utf-8')
            output = root / '.local/blocked/output.ktfix'
        elif name == 'relative-component': output = root / '.local/../.local/output.ktfix'
        elif name == 'wrong-extension': output = output.with_suffix('.bin')
        elif name == 'duplicate-option': extra = ['--community-replay', '--community-replay']
        elif name == 'empty-option': extra = ['--root', '']
        elif name == 'invalid-sibling':
            (root / 'config.ini').write_text('[game.futari]\ndata_dir=Q:relative\n', encoding='utf-8')
        before = digest(source)
        config = root / 'config.ini'; before_config = digest(config) if config.exists() else None
        result = subprocess.run([str(executable), '--prepare-fixes', str(data), '--root', str(root),
            '--output', str(output), *extra], cwd=directory, capture_output=True, timeout=20)
        passed = result.returncode == 2 and expected_error in result.stderr.decode('utf-8', errors='replace') and digest(source) == before
        passed = passed and (digest(config) if config.exists() else None) == before_config
        passed = passed and (output.read_bytes() == sentinel if name == 'existing' else not output.exists())
        if name == 'junction': passed = passed and not any(target.iterdir())
        for suffix, content in [('stdout.jsonl', result.stdout), ('stderr.txt', result.stderr)]:
            with (directory / ('path-' + name + '.' + suffix)).open('xb') as stream: stream.write(content)
        checks.append(dict(check='path-' + name, passed=passed, exitCode=result.returncode))
        print(('PASS' if passed else 'FAIL') + ': resource-path-' + name, flush=True)
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    receipt = verify()
    directory = private_output(args.output or ROOT / '.local/ps4-resource-checks' /
        (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8]))
    directory.mkdir(parents=True, exist_ok=False)
    cases = fixtures.cases()
    request = directory / 'authored-inputs.json'
    write_json_new(request, cases)
    executable = ROOT / 'out/ps4-native/KetsuiResourcePrepareCheck.exe'
    command = [str(executable), str(request)]
    result = subprocess.run(command, cwd=directory, capture_output=True, timeout=60)
    for suffix, data in [('stdout.json', result.stdout), ('stderr.txt', result.stderr)]:
        with (directory / suffix).open('xb') as output: output.write(data)
    if result.returncode: raise RuntimeError('Resource driver failed: ' + str(directory))
    response = json.loads(result.stdout)
    if response['guestExecution'] is not False or len(response['results']) != len(cases):
        raise ValueError('Incomplete resource check response')
    repeated = subprocess.run(command, cwd=directory, capture_output=True, timeout=60)
    if repeated.returncode or repeated.stdout != result.stdout: raise ValueError('Resource preparation is not deterministic')
    checks = []
    for case, native in zip(cases, response['results']):
        try:
            expected = fixtures.oracle(case)
            valid = True
        except Exception:
            valid, expected = False, None
        passed = native['name'] == case['name'] and native['valid'] == valid
        byte_identical = None
        if valid and passed:
            byte_identical = native['value'] == expected
            if case['op'] == 'wrap':
                # zlib versions may emit different streams; fixed extent and
                # decoded script bytes, not compressed representation, matter.
                decoded = fixtures.fixes.unwrap(bytes.fromhex(native['value']), bytes.fromhex(case['seed']), case['resource'])
                passed = len(bytes.fromhex(native['value'])) == case['extent'] and decoded.hex() == case['source']
            else:
                passed = byte_identical
        checks.append(dict(check=case['name'], passed=passed, expectedValid=valid, byteIdentical=byte_identical))
        print(('PASS' if passed else 'FAIL') + ': resource-' + case['name'], flush=True)
    checks.extend(path_checks(ROOT / 'out/ps4-native/KetsuiNativeProbe.exe', directory))
    write_json_new(directory / 'result.json', dict(format=1, gameExecution=False, deterministic=True,
        sourceCommit=receipt['sourceCommit'], sourceDirty=receipt['sourceDirty'], executableSha256=digest(executable), checks=checks))
    print(f'{sum(item["passed"] for item in checks)}/{len(checks)} native resource checks passed; report: {directory}')
    return 0 if all(item['passed'] for item in checks) else 1


if __name__ == '__main__': sys.exit(main())
