import io
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server import reports


class LogReportTests(unittest.TestCase):
    def archive(self, extra=None, **metadata):
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr('./report.json', json.dumps({'schema': 1, 'game': 'espgaluda',
                'run': 'run-20261102T120000Z-2', 'version': '0.9.0', 'nickname': 'Player',
                'message': '最终 BOSS 崩溃', **metadata}))
            archive.writestr('./logs/stderr.log', 'Missing statically compiled guest target')
            if extra:
                archive.writestr(*extra)
        return data.getvalue()

    def test_metadata_and_archive_are_retained_without_extracting(self):
        with tempfile.TemporaryDirectory() as temporary:
            body = self.archive()
            result = reports.store(temporary, body)
            self.assertEqual(result['message'], '最终 BOSS 崩溃')
            self.assertEqual((Path(temporary) / (result['id'] + '.zip')).read_bytes(), body)
            self.assertEqual(len(list(Path(temporary).iterdir())), 2)

    def test_rejects_traversal_binary_game_data_and_bad_metadata(self):
        for name in ('../private.txt', './logs/../escape.log', './logs/game.bin', './game-data/disc.iso'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                reports.inspect(self.archive((name, b'authored')))
        with self.assertRaises(ValueError):
            reports.inspect(self.archive(message=5))
        with self.assertRaises(ValueError):
            reports.inspect(self.archive(game='unknown'))

    def test_http_receipt_and_private_download(self):
        with tempfile.TemporaryDirectory() as temporary:
            server = reports.serve(temporary, 0)
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                root = 'http://127.0.0.1:' + str(server.server_port)
                with urllib.request.urlopen(urllib.request.Request(root + '/cave/api/v1/reports',
                        data=self.archive(), headers={'Content-Type': 'application/zip'})) as response:
                    self.assertEqual(response.status, 201)
                    identifier = json.load(response)['id']
                self.assertTrue((Path(temporary) / (identifier + '.zip')).is_file())
                with self.assertRaises(urllib.error.HTTPError) as error:
                    urllib.request.urlopen(root + '/cave/api/v1/reports/' + identifier)
                self.assertEqual(error.exception.code, 404)
            finally:
                server.shutdown()
                worker.join()
                server.server_close()


if __name__ == '__main__':
    unittest.main()
