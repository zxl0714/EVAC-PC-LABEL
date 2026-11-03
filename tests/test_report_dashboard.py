import json
from pathlib import Path
import tempfile
import threading
import unittest
import urllib.error
import urllib.request

from server import report_dashboard


class DashboardTests(unittest.TestCase):
    def test_list_search_pagination_escaping_and_download(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for number, game in enumerate(('ketsui', 'espgaluda')):
                identifier = f'20261103-{number:016x}'
                (root / (identifier + '.json')).write_text(json.dumps(dict(id=identifier,
                    received=f'2026-11-03T0{number}:00:00+00:00', game=game, version='0.9.1',
                    nickname='Player', message='<script>alert(1)</script>', run='run-test', bytes=3)))
                (root / (identifier + '.zip')).write_bytes(b'ZIP')
            (root / 'bad.json').write_text('{}')
            server = report_dashboard.serve(root, 0)
            worker = threading.Thread(target=server.serve_forever, daemon=True); worker.start()
            base = f'http://127.0.0.1:{server.server_port}/cave/reports/'
            try:
                data = json.load(urllib.request.urlopen(base+'api/reports?limit=1'))
                self.assertEqual((data['total'], data['reports'][0]['game']), (2, 'espgaluda'))
                self.assertEqual(json.load(urllib.request.urlopen(base+'api/reports?game=ketsui'))['total'], 1)
                self.assertEqual(json.load(urllib.request.urlopen(base+'api/reports?q=PLAYER'))['total'], 2)
                page = urllib.request.urlopen(base).read().decode()
                self.assertIn('&lt;script&gt;', page); self.assertNotIn('<script>', page)
                self.assertEqual(urllib.request.urlopen(base+'download/20261103-0000000000000000.zip').read(), b'ZIP')
                for path in ('download/../../secret.zip', 'download/nope.zip'):
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        urllib.request.urlopen(base+path)
                    self.assertEqual(error.exception.code, 404)
            finally:
                server.shutdown(); worker.join(); server.server_close()


if __name__ == '__main__':
    unittest.main()
