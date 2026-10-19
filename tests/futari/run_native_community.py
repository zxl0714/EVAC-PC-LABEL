"""Futari's original native transfer regressions against a disposable service."""
import argparse
import base64
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from server.games import futari as live


class TransferStore(live.Store):
    # Fault cases from the pinned Futari test_live_native.py. These fixture
    # view IDs never reach the deployed service or any game profile.
    retry_failure_used = False
    staged_retry_failure_used = False
    score_retry_failure_used = False

    def submit(self, body):
        if body.get('view') == 7030 and not self.score_retry_failure_used:
            self.score_retry_failure_used = True
            raise live.ApiError(503, 'temporary_score_failure')
        return super().submit(body)

    def upload_replay(self, body):
        if body.get('mode') == 'view-7014' and not self.retry_failure_used:
            self.retry_failure_used = True
            raise live.ApiError(503, 'temporary_replay_failure')
        if body.get('mode') == 'view-7021' and not self.staged_retry_failure_used:
            self.staged_retry_failure_used = True
            raise live.ApiError(503, 'temporary_staged_replay_failure')
        return super().upload_replay(body)

    def replay(self, rid):
        result = super().replay(rid)
        mode = result['mode']
        if mode == 'view-7009':
            result['sha256'] = '0' * 64
        elif mode == 'view-7010':
            result['mode'] = 'view-7011'
        elif mode == 'view-7011':
            result['build'] = 'unrelated-build'
        elif mode == 'view-7012':
            result['payload'] = '%%%='
        elif mode == 'view-7013':
            payload = bytearray(base64.b64decode(result['payload']))
            payload[4] ^= 0xff
            result['sha256'] = hashlib.sha256(payload).hexdigest()
            result['payload'] = base64.b64encode(payload).decode('ascii')
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    executable = args.exe.resolve(strict=True)
    if not executable.is_file() or not executable.is_relative_to(ROOT / 'out'):
        raise ValueError('Use the collection build test executable')
    with tempfile.TemporaryDirectory(prefix='cave-futari-native-') as temp:
        server = live.Server(('127.0.0.1', 0), TransferStore(Path(temp) / 'test.sqlite3'))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            result = subprocess.run([str(executable), f'http://127.0.0.1:{server.server_port}{live.API}', temp],
                                    capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
            print(result.stdout)
            if result.returncode:
                raise RuntimeError(f'Native tests failed ({result.returncode}): {result.stderr}')
        finally:
            server.shutdown()
            server.server_close()
            thread.join(3)
            if thread.is_alive():
                raise RuntimeError('Fixture service did not stop')


if __name__ == '__main__':
    main()
