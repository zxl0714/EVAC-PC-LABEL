"""Exercise immutable prepared generations using explicitly authored collections."""
import argparse
import ctypes
import datetime
import json
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analysis_paths import private_output
from project import ROOT, digest, write_json_new
from ps4_build_receipt import verify

FILES = ('eboot.ktprobe', 'modules/own.ktmodule', 'game-fixes.ktfix')


class Checks:
    def __init__(self, directory, executable):
        self.directory, self.executable = directory, executable
        self.serial = 0
        self.results = []

    def fixture(self, name, config=''):
        root = self.directory / ('collection-' + name)
        root.mkdir()
        (root / '.authored-cache-fixture').write_bytes(b'CAVE PC authored cache fixture\n')
        data = root / 'data'
        data.mkdir()
        (data / 'source.txt').write_bytes(b'authored source, not a game program')
        (root / 'config.ini').write_text(config, encoding='utf-8')
        recipe = dict(format=1, game='ketsui', version=1, sourceSha256=digest(data / 'source.txt'), producerSha256='authored-one')
        return root, dict(root=str(root), data=str(data), recipe=recipe)

    def run(self, request, expected=0):
        self.serial += 1
        name = 'request-' + str(self.serial)
        path = self.directory / (name + '.json')
        write_json_new(path, request)
        source = Path(request['data']) / 'source.txt'
        before = digest(source)
        result = subprocess.run([str(self.executable), '--cache-request', str(path)], cwd=self.directory,
                                capture_output=True, timeout=30)
        for suffix, content in [('stdout.json', result.stdout), ('stderr.txt', result.stderr)]:
            with (self.directory / (name + '.' + suffix)).open('xb') as stream: stream.write(content)
        assert result.returncode == expected, result.stderr.decode('utf-8', errors='replace')
        assert digest(source) == before, 'Authored source modified'
        if expected:
            return result.stderr.decode('utf-8', errors='replace')
        response = json.loads(result.stdout)
        assert response['guestExecution'] is False
        directory = Path(response['directory'])
        container = Path(request['root']).resolve() / 'prepared' / request['recipe']['game']
        directory.resolve().relative_to(container)
        assert response['buildCalls'] == (0 if response['cacheHit'] else 1)
        for file in FILES:
            assert (directory / file).read_bytes() == ('authored:' + file + ':' + request.get('payload', 'one')).encode()
        receipt = response['receipt']
        assert receipt['game'] == request['recipe']['game']
        assert receipt['recipe'] == request['recipe'] and receipt['recipeSha256'] == response['recipeSha256']
        assert [item['path'] for item in receipt['outputs']] == list(FILES)
        for item in receipt['outputs']:
            output = directory / item['path']
            assert output.stat().st_size == item['bytes'] and digest(output) == item['sha256']
        assert not (container / '.prepare.lock').exists()
        return response

    def check(self, name, action):
        try:
            action()
            result = dict(check=name, passed=True)
        except Exception as error:
            result = dict(check=name, passed=False, error=str(error))
        self.results.append(result)
        print(('PASS' if result['passed'] else 'FAIL') + ': cache-' + name, flush=True)

    def first_and_reuse(self):
        root, request = self.fixture('first-reuse')
        first = self.run(request)
        assert first['cacheHit'] is False
        active = root / 'prepared/ketsui/active.json'
        before = active.read_bytes()
        stamp = active.stat().st_mtime_ns
        second = self.run(request)
        assert second['cacheHit'] is True and second['directory'] == first['directory']
        assert active.read_bytes() == before and active.stat().st_mtime_ns == stamp

    def damage(self, kind):
        root, request = self.fixture(kind)
        first = self.run(request)
        directory = Path(first['directory'])
        damaged = directory / FILES[0]
        if kind == 'same-size-corruption':
            raw = damaged.read_bytes(); damaged.write_bytes(bytes([raw[0] ^ 1]) + raw[1:])
        elif kind == 'truncation': damaged.write_bytes(b'authored truncated')
        elif kind == 'missing-file': damaged.unlink()
        elif kind == 'unexpected-file': (directory / 'extra.txt').write_bytes(b'authored extra')
        elif kind == 'bad-receipt': (directory / 'receipt.json').write_text('{bad json', encoding='utf-8')
        elif kind == 'wrong-receipt-hash':
            receipt = json.loads((directory / 'receipt.json').read_text())
            receipt['outputs'][0]['sha256'] = '0' * 64
            (directory / 'receipt.json').write_text(json.dumps(receipt), encoding='utf-8')
        elif kind == 'wrong-receipt-recipe':
            receipt = json.loads((directory / 'receipt.json').read_text())
            receipt['recipe']['version'] = 99
            (directory / 'receipt.json').write_text(json.dumps(receipt), encoding='utf-8')
        old = {str(path.relative_to(directory)): digest(path) for path in directory.rglob('*') if path.is_file()}
        second = self.run(request)
        assert second['cacheHit'] is False and second['directory'] != first['directory']
        assert {str(path.relative_to(directory)): digest(path) for path in directory.rglob('*') if path.is_file()} == old

    def changed_recipe(self, field):
        root, request = self.fixture('changed-' + field)
        first = self.run(request)
        before = digest(Path(first['directory']) / 'receipt.json')
        request['recipe'][field] = 'authored-two' if field == 'producerSha256' else 2
        request['payload'] = 'two'
        second = self.run(request)
        assert not second['cacheHit'] and second['directory'] != first['directory']
        assert digest(Path(first['directory']) / 'receipt.json') == before

    def changed_source(self):
        root, request = self.fixture('source-change')
        first = self.run(request)
        source = Path(request['data']) / 'source.txt'
        source.write_bytes(b'authored new source')
        request['recipe']['sourceSha256'] = digest(source)
        request['payload'] = 'new-source'
        second = self.run(request)
        assert not second['cacheHit'] and second['directory'] != first['directory']

    def bad_pointer(self, value):
        root, request = self.fixture('pointer-' + ('absolute' if Path(value).is_absolute() else 'relative'))
        first = self.run(request)
        active = root / 'prepared/ketsui/active.json'
        pointer = json.loads(active.read_text()); pointer['directory'] = value
        active.write_text(json.dumps(pointer), encoding='utf-8')
        second = self.run(request)
        assert not second['cacheHit'] and second['directory'] != first['directory']

    def failure(self):
        root, request = self.fixture('failure')
        first = self.run(request)
        active = root / 'prepared/ketsui/active.json'
        before = active.read_bytes()
        request['recipe']['version'] = 2
        failed = {**request, 'failBuild': True}
        assert 'Injected authored preparation failure' in self.run(failed, 2)
        assert active.read_bytes() == before
        partials = list(active.parent.glob('.pending-*'))
        assert len(partials) == 1 and not (partials[0] / 'receipt.json').exists()
        before_partial = digest(partials[0] / FILES[0])
        recovered = self.run(request)
        assert not recovered['cacheHit'] and recovered['directory'] != first['directory']
        assert digest(partials[0] / FILES[0]) == before_partial

    def lock(self):
        root, request = self.fixture('concurrent')
        self.run(request)
        api = ctypes.WinDLL('kernel32', use_last_error=True)
        api.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                                   ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        api.CreateFileW.restype = ctypes.c_void_p
        api.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = api.CreateFileW(str(root / 'prepared/ketsui/.prepare.lock'), 0xC0000000, 0, None, 4, 0x04000002, None)
        assert handle not in (None, ctypes.c_void_p(-1).value)
        active = (root / 'prepared/ketsui/active.json').read_bytes()
        try: assert 'cannot be locked' in self.run(request, 2)
        finally: api.CloseHandle(handle)
        assert (root / 'prepared/ketsui/active.json').read_bytes() == active
        assert self.run(request)['cacheHit']

    def junction(self, container):
        root, request = self.fixture('junction-' + container)
        target = self.directory / ('junction-target-' + container)
        target.mkdir(); (target / 'sentinel.txt').write_bytes(b'authored external sentinel')
        if container == 'generation':
            first = self.run(request)
            directory = Path(first['directory'])
            source, destination = directory / 'modules', root / 'old-authored-modules'
            source.resolve().relative_to(self.directory.resolve())
            destination.resolve().relative_to(self.directory.resolve())
            source.rename(destination)
            link = directory / 'modules'
        else:
            (root / 'prepared').mkdir()
            link = root / 'prepared/ketsui'
        created = subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(target)], capture_output=True)
        assert created.returncode == 0
        if container == 'generation': assert not self.run(request)['cacheHit']
        else: assert 'cannot traverse' in self.run(request, 2)
        assert list(target.iterdir()) == [target / 'sentinel.txt'] and (target / 'sentinel.txt').read_bytes() == b'authored external sentinel'

    def overlap(self, kind):
        config = '[game.futari]\n' + ('dlc_dir' if kind == 'dlc' else 'data_dir') + '=prepared/ketsui\n'
        root, request = self.fixture('overlap-' + kind, config)
        assert 'configured ' in self.run(request, 2)
        assert not (root / 'prepared').exists()

    def invalid_sibling(self):
        root, request = self.fixture('invalid-sibling', '[game.futari]\ndata_dir=Q:relative\n')
        assert not self.run(request)['cacheHit']

    def relocation(self):
        root, request = self.fixture('before-move')
        first = self.run(request)
        destination = self.directory / '移动后 collection'
        shutil.copytree(root, destination)
        moved = {**request, 'root': str(destination), 'data': str(destination / 'data')}
        second = self.run(moved)
        assert second['cacheHit'] and second['recipeSha256'] == first['recipeSha256']
        assert Path(second['directory']).parent == destination / 'prepared/ketsui'

    def game_isolation(self):
        root, request = self.fixture('game-isolation')
        first = self.run(request)
        active = root / 'prepared/ketsui/active.json'
        before = active.read_bytes()
        other = {**request, 'recipe': {**request['recipe'], 'game': 'daioujou'}}
        second = self.run(other)
        assert not second['cacheHit'] and second['recipeSha256'] != first['recipeSha256']
        assert Path(second['directory']).parent == root / 'prepared/daioujou'
        assert active.read_bytes() == before and self.run(request)['cacheHit']
        assert self.run(other)['cacheHit']

    def invalid_game(self):
        for index, game in enumerate((None, '../ketsui', 'not-a-title', 1)):
            root, request = self.fixture('invalid-game-' + str(index))
            request['recipe']['game'] = game
            self.run(request, 2)
            assert not (root / 'prepared').exists()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    receipt = verify()
    directory = private_output(args.output or ROOT / '.local/ps4-cache-checks' /
        (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8]))
    directory.mkdir(parents=True, exist_ok=False)
    executable = ROOT / 'out/ps4-native/KetsuiResourcePrepareCheck.exe'
    suite = Checks(directory, executable)
    suite.check('first-and-reuse', suite.first_and_reuse)
    for kind in ('same-size-corruption', 'truncation', 'missing-file', 'unexpected-file', 'bad-receipt',
                 'wrong-receipt-hash', 'wrong-receipt-recipe'):
        suite.check(kind, lambda kind=kind: suite.damage(kind))
    for field in ('version', 'producerSha256'): suite.check('changed-' + field, lambda field=field: suite.changed_recipe(field))
    suite.check('changed-source', suite.changed_source)
    suite.check('pointer-traversal', lambda: suite.bad_pointer('../data'))
    suite.check('pointer-absolute', lambda: suite.bad_pointer(str(directory / 'outside')))
    suite.check('failed-build-recovery', suite.failure)
    suite.check('concurrent-lock', suite.lock)
    for kind in ('container', 'generation'): suite.check('junction-' + kind, lambda kind=kind: suite.junction(kind))
    for kind in ('data', 'dlc'): suite.check('overlap-' + kind, lambda kind=kind: suite.overlap(kind))
    suite.check('invalid-sibling', suite.invalid_sibling)
    suite.check('relocation', suite.relocation)
    suite.check('game-isolation', suite.game_isolation)
    suite.check('invalid-game', suite.invalid_game)
    write_json_new(directory / 'result.json', dict(format=1, gameExecution=False, authoredInputs=True,
        sourceCommit=receipt['sourceCommit'], sourceDirty=receipt['sourceDirty'], executableSha256=digest(executable), checks=suite.results))
    print(f'{sum(item["passed"] for item in suite.results)}/{len(suite.results)} native cache checks passed; report: {directory}')
    return 0 if all(item['passed'] for item in suite.results) else 1


if __name__ == '__main__': sys.exit(main())
