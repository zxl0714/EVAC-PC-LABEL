"""Run the unchanged Futari protocol on an isolated local collection endpoint."""
import argparse
import importlib.util
import json
import logging
from pathlib import Path
import signal
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analysis_paths import private_output
from project import ROOT, write_json_new

API = '/cave/api/v1/games/futari'


def create_service(directory, port=0):
    if not 0 <= port <= 65535:
        raise ValueError('Port must be in 0..65535')
    directory = private_output(directory)
    directory.mkdir(parents=True, exist_ok=False)
    # A private module instance keeps the production/default route and other
    # test servers untouched; only this process serves the collection alias.
    spec = importlib.util.spec_from_file_location(
        '_futari_local_protocol', ROOT / 'server/games/futari.py')
    protocol = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(protocol)
    protocol.API = API
    server = protocol.Server(('127.0.0.1', port), protocol.Store(directory / 'community.sqlite3'))
    metadata = dict(format=1, game='futari', localOnly=True,
                    endpoint=f'http://127.0.0.1:{server.server_port}/cave/api/v1',
                    api=API, database='community.sqlite3')
    try:
        write_json_new(directory / 'service.json', metadata)
    except BaseException:
        server.server_close()
        raise
    return server, metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True,
                        help='New directory below collection/.local; never reuses a database')
    parser.add_argument('--seconds', type=int, default=1800,
                        help='Automatic shutdown after 1..7200 seconds')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 7200:
        parser.error('--seconds must be in 1..7200')
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)s %(message)s')
    server, metadata = create_service(args.directory)
    def stop(_signal=None, _frame=None):
        threading.Thread(target=server.shutdown, daemon=True).start()
    for number in (signal.SIGINT, signal.SIGTERM):
        signal.signal(number, stop)
    timer = threading.Timer(args.seconds, stop)
    timer.daemon = True
    timer.start()
    print(json.dumps(metadata), flush=True)
    try:
        server.serve_forever(poll_interval=0.2)
    finally:
        timer.cancel()
        server.server_close()


if __name__ == '__main__':
    main()
