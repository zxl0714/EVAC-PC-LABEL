"""Manual player log reports. Upload over HTTPS; retrieve privately over SSH."""
import argparse
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import shutil
from pathlib import Path, PurePosixPath
import threading
import uuid
import zipfile

MAX_ZIP = 32 * 1024 * 1024
MAX_TEXT = 128 * 1024 * 1024
GAMES = {'futari', 'espgaluda2', 'sdoj', 'guwange', 'ketsui', 'daioujou',
         'esprade', 'feveron', 'garegga', 'espgaluda', 'ibara'}
EXTENSIONS = {'.json', '.jsonl', '.log', '.txt', '.csv', '.ini'}


def inspect(body):
    if len(body) > MAX_ZIP:
        raise ValueError('Report exceeds 32 MiB')
    with zipfile.ZipFile(io.BytesIO(body)) as archive:
        entries = archive.infolist()
        if len(entries) > 300 or sum(e.file_size for e in entries) > MAX_TEXT + 65536:
            raise ValueError('Too many files or expanded report exceeds 128 MiB')
        seen = set()
        metadata = None
        logs = 0
        for entry in entries:
            name = entry.filename.removeprefix('./')
            path = PurePosixPath(name)
            if (path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name
                    or name in seen or (entry.external_attr >> 16) & 0o170000 == 0o120000
                    or entry.flag_bits & 1):
                raise ValueError('Invalid report entry')
            seen.add(name)
            if entry.is_dir():
                continue
            if name == 'report.json':
                if entry.file_size > 65536:
                    raise ValueError('Report metadata is too large')
                metadata = json.loads(archive.read(entry))
            elif path.parts[0] == 'logs' and path.suffix.lower() in EXTENSIONS:
                # Verify compressed members and CRC once; nothing is extracted.
                with archive.open(entry) as source:
                    while source.read(1024 * 1024):
                        pass
                logs += 1
            else:
                raise ValueError('Only text logs are accepted')
        if not logs or not isinstance(metadata, dict) or metadata.get('schema') != 1:
            raise ValueError('Report metadata or logs are missing')
        if metadata.get('game') not in GAMES:
            raise ValueError('Unknown game')
        for key, limit in (('message', 8000), ('nickname', 256), ('version', 128), ('run', 256)):
            if not isinstance(metadata.get(key), str) or len(metadata[key].encode('utf-8')) > limit:
                raise ValueError('Invalid ' + key)
        if not metadata['run'].startswith('run-'):
            raise ValueError('Invalid play session')
    return {key: metadata[key] for key in ('game', 'run', 'nickname', 'message', 'version')}


def store(directory, body):
    metadata = inspect(body)
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    # One generous total ceiling keeps this small anonymous service bounded.
    if (sum(p.stat().st_size for p in directory.glob('*.zip')) + len(body) > 4 * 1024**3
            or shutil.disk_usage(directory).free < len(body) + 512 * 1024**2):
        raise OSError('Report storage is full')
    identifier = datetime.now(timezone.utc).strftime('%Y%m%d-') + uuid.uuid4().hex[:16]
    metadata.update(id=identifier, received=datetime.now(timezone.utc).isoformat(), bytes=len(body))
    zip_path = directory / (identifier + '.zip')
    with zip_path.open('xb') as output:
        output.write(body)
    (directory / (identifier + '.json')).write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding='utf-8')
    return metadata


class Handler(BaseHTTPRequestHandler):
    def reply(self, code, value):
        data = json.dumps(value).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.reply(200 if self.path == '/cave/api/v1/reports/health' else 404,
                   {'service': 'evac-reports'} if self.path.endswith('/health') else {'error': 'not found'})

    def do_POST(self):
        if self.path != '/cave/api/v1/reports':
            self.reply(404, {'error': 'not found'})
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= MAX_ZIP:
                self.reply(413, {'error': 'Report exceeds 32 MiB'})
                return
            self.connection.settimeout(30)
            body = self.rfile.read(length)
            if len(body) != length:
                raise ValueError('Incomplete upload')
            with self.server.store_lock:
                result = store(self.server.directory, body)
            self.reply(201, {'id': result['id']})
        except (ValueError, KeyError, zipfile.BadZipFile, UnicodeError, RuntimeError) as error:
            self.reply(400, {'error': str(error)[:200]})
        except OSError:
            self.reply(503, {'error': 'Report storage unavailable'})


def serve(directory, port=18093):
    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    server.directory = Path(directory)
    server.store_lock = threading.Lock()
    return server


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--port', type=int, default=18093)
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()
    if args.list:
        for path in sorted(args.directory.glob('*.json'), reverse=True):
            print(path.read_text(encoding='utf-8'))
    else:
        serve(args.directory, args.port).serve_forever()
