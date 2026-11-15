import io
import json
from pathlib import Path
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from server import file_uploads, report_dashboard, reports


class Storage:
    oss_error = OSError

    def __init__(self):
        self.parts = {}
        self.objects = {}
        self.calls = []

    def start(self, key):
        self.parts[key] = {}
        return 'multipart-id'

    def put(self, key, upload, number, body):
        self.calls.append(number)
        self.parts[key][number] = body
        if self.calls == [1, 2]:
            raise OSError('Connection severed after storing part')
        return 'etag-' + str(number)

    def complete(self, key, upload, parts, size):
        self.objects[key] = b''.join(self.parts[key][number] for number, _ in parts)
        assert len(self.objects[key]) == size

    def abort(self, key, upload):
        self.parts.pop(key, None)

    def download(self, key):
        return io.BytesIO(self.objects[key])


class FileUploadsTest(unittest.TestCase):
    def test_retry_and_dashboard_download(self):
        with tempfile.TemporaryDirectory() as temporary:
            storage = Storage()
            upload = reports.serve(temporary, 0, storage)
            upload.file_uploads.part_size = 4
            dashboard = report_dashboard.serve(temporary, 0, storage)
            for server in (upload, dashboard):
                threading.Thread(target=server.serve_forever, daemon=True).start()
                self.addCleanup(server.server_close)
                self.addCleanup(server.shutdown)
            base = f'http://127.0.0.1:{upload.server_port}' + file_uploads.ROUTE
            admin = f'http://127.0.0.1:{dashboard.server_port}'
            token = {'Authorization': 'Bearer ' + 'c' * 32}

            def request(method, suffix='', body=None, headers=token):
                return urlopen(Request(base + suffix, data=body, headers=headers, method=method), timeout=4)

            metadata = json.dumps(dict(game='esprade', version='0.9.11', name='解析用.pk3',
                                       message='<script>', bytes=10)).encode()
            try:
                for method, suffix, body, status in (
                    ('PUT', '/1', b'1234', 404),
                    ('POST', '', metadata, 200),
                    ('POST', '', metadata, 200),
                    ('PUT', '/1', b'1234', 200),
                    ('PUT', '/1', b'1234', 200),
                    ('PUT', '/1', b'fake', 409),
                    ('PUT', '/2', b'5678', 503),
                    ('PUT', '/2', b'5678', 200),
                    ('POST', '/complete', None, 409),
                    ('PUT', '/3', b'90', 200),
                ):
                    if status < 400:
                        with request(method, suffix, body) as response:
                            self.assertEqual(response.status, status)
                    else:
                        with self.assertRaises(HTTPError) as error:
                            request(method, suffix, body)
                        self.assertEqual(error.exception.code, status)
                with request('POST', '/complete') as response:
                    identifier = json.load(response)['id']
                with request('POST', '/complete') as response:
                    self.assertEqual(json.load(response)['id'], identifier)
                self.assertEqual(storage.calls, [1, 2, 2, 3])
                with urlopen(admin + '/cave/reports/api/reports?' + urlencode({'q': '解析'})) as response:
                    item, = json.load(response)['reports']
                self.assertEqual((item['id'], item['kind'], item['message']), (identifier, 'file', '<script>'))
                with urlopen(admin + item['download_url']) as response:
                    self.assertEqual(response.read(), b'1234567890')
                    self.assertIn('filename*=UTF-8', response.headers['Content-Disposition'])
                with urlopen(admin + '/cave/reports/') as response:
                    self.assertIn('&lt;script&gt;', response.read().decode())
                log_id = '20261103-0000000000000000'
                log = Path(temporary) / log_id
                log.with_suffix('.json').write_text(json.dumps(dict(
                    id=log_id, received='2026-11-03T00:00:00+00:00', game='esprade', message='A log report')))
                log.with_suffix('.zip').write_bytes(b'ZIP')
                with urlopen(admin + '/cave/reports/uploads/') as response:
                    page = response.read().decode()
                self.assertIn('解析用.pk3', page)
                self.assertIn('10 bytes', page)
                self.assertIn('&lt;script&gt;', page)
                self.assertNotIn('<script>', page)
                self.assertNotIn('A log report', page)
                self.assertIn(f'href="{item["download_url"]}"', page)
                with urlopen(admin + '/cave/reports/uploads/?game=ketsui') as response:
                    self.assertIn('No matching files.', response.read().decode())
                for kind, ids in (('file', [identifier]), ('log', [log_id]), ('', [identifier, log_id])):
                    with urlopen(admin + '/cave/reports/api/reports?' + urlencode({'kind': kind})) as response:
                        self.assertCountEqual([item['id'] for item in json.load(response)['reports']], ids)
                with self.assertRaises(HTTPError) as error:
                    urlopen(admin + '/cave/reports/api/reports?kind=unknown')
                self.assertEqual(error.exception.code, 400)
                with self.assertRaises(HTTPError) as error:
                    request('GET', '/' + identifier)
                self.assertEqual(error.exception.code, 404)
            finally:
                self.doCleanups()

    def test_metadata_preserves_large_size_and_cancel_cleans_state(self):
        with tempfile.TemporaryDirectory() as temporary:
            storage = Storage()
            service = file_uploads.Uploads(temporary, storage, reports.GAMES)
            path = service.pending / 'test.json'
            size = 6 * 1024**3 + 1
            service.begin(path, {'game': 'ketsui', 'name': 'no-extension', 'bytes': size})
            state = service.load(path)
            self.assertEqual(state['metadata']['bytes'], size)
            self.assertEqual(service.extent(state, 385), 1)
            service.cancel(path)
            self.assertFalse(Path(path).exists())
            self.assertFalse(storage.parts)
