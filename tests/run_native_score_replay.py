"""A disposable loopback service for authored native score/replay probes."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server.games.espgaluda2 import Server, Store as G2Store
from server.games.sdoj import Store as SdojStore


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', choices=('espgaluda2', 'sdoj'), required=True)
    parser.add_argument('--program', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cave-native-replay-') as directory:
        store = {'espgaluda2': G2Store, 'sdoj': SdojStore}[args.game](Path(directory) / 'community.sqlite3')
        server = Server(('127.0.0.1', 0), store)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            endpoint = f'http://127.0.0.1:{server.server_address[1]}/cave/api/v1'
            result = subprocess.run([str(args.program.resolve()), '--probe', endpoint], timeout=45)
            return result.returncode
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=5)


if __name__ == '__main__':
    raise SystemExit(main())
