import io
import json
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server import reports


class LogReportTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('EVAC_REPORT_TEST_EXE') and os.environ.get('EVAC_TEST_WINDOWS_FAULT') == '1',
                         'requires the opt-in Windows Error Reporting crash fixture')
    def test_fast_fail_includes_windows_fault_after_process_disappears(self):
        fixture = Path(os.environ['EVAC_REPORT_TEST_EXE'])
        result = subprocess.run([fixture, '--fixture-fault'], capture_output=True, check=True,
                                timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
        body = Path(result.stdout.decode('utf-8').strip()).read_bytes()
        with zipfile.ZipFile(io.BytesIO(body)) as archive:
            receipt = archive.read('./logs/launcher-exit.txt').decode()
            evidence = dict(line.split('=', 1) for line in archive.read('./logs/process-at-upload.txt').decode().splitlines())
        self.assertIn('exit_code_hex=0xc0000409', receipt)
        self.assertIn('launcher_close_shortcut=none', receipt)
        self.assertEqual(evidence['windows_fault'], 'application_error_1000')
        self.assertEqual(evidence['windows_fault_module'].lower(), fixture.name.lower())
        self.assertEqual(int(evidence['windows_exception_code_hex'], 16), 0xc0000409)
        self.assertGreater(int(evidence['windows_fault_offset_hex'], 16), 0)
        self.assertEqual(reports.inspect(body)['game'], 'daioujou')

    @unittest.skipUnless(os.environ.get('EVAC_REPORT_TEST_EXE'), 'requires compiled report fixture')
    def test_abrupt_exit_survives_closed_process_handle(self):
        result = subprocess.run([os.environ['EVAC_REPORT_TEST_EXE'], '--fixture-exit'],
            capture_output=True, check=True, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
        path = Path(result.stdout.decode('utf-8').strip())
        with zipfile.ZipFile(path) as archive:
            receipt = archive.read('./logs/launcher-exit.txt').decode()
            self.assertIn('exit_code_hex=0x00004123', receipt)
            self.assertIn('launcher_close_shortcut=controller', receipt)
            self.assertIn('vkCreateSwapchainKHR enter', archive.read('./logs/stderr.txt').decode())
        self.assertEqual(reports.inspect(path.read_bytes())['game'], 'daioujou')

    @unittest.skipUnless(os.environ.get('EVAC_REPORT_TEST_EXE'), 'requires compiled report fixture')
    def test_launcher_automatically_summarizes_old_dumps(self):
        result = subprocess.run([os.environ['EVAC_REPORT_TEST_EXE'], '--fixture'],
            capture_output=True, check=True, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
        archive_path = Path(result.stdout.decode('utf-8').strip())
        body = archive_path.read_bytes()
        with zipfile.ZipFile(io.BytesIO(body)) as archive:
            names = archive.namelist()
            summaries = [archive.read(n).decode('utf-8') for n in names if '/auto-crash-' in n]
            self.assertEqual(len(summaries), 2)
            self.assertTrue(any('fault_module=authored-driver.dll+0x1234' in s for s in summaries))
            self.assertTrue(any('summary_error=' in s for s in summaries))
            self.assertFalse(any(n.endswith(('.dmp', '.bin')) for n in names))
            self.assertIn('Missing statically compiled', archive.read('./logs/stderr.log').decode())
            runtime=archive.read('./logs/runtime-at-upload.txt').decode()
            self.assertIn('Windows=', runtime)
            self.assertIn('runtime', runtime)
        self.assertEqual(reports.inspect(body)['game'], 'espgaluda')

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
