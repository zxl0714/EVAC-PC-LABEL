"""The player GUI and command mode share this verified STFS extraction core."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_xbla_extract import fixture

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'out/common/bin/Extract-XBLA.exe'


@unittest.skipUnless(EXE.is_file(), 'Build xbla_extract first')
class NativeXblaTests(unittest.TestCase):
    def extract(self, source, output):
        return subprocess.run([str(EXE), '--extract', '--input', str(source), '--output', str(output)],
                              timeout=30, capture_output=True)

    def test_valid_data_and_unicode_paths(self):
        with tempfile.TemporaryDirectory(prefix='evac-xbla-native-') as temporary:
            root = Path(temporary) / '游戏 files'; root.mkdir()
            for boundary in (False, True, 'level2'):
                with self.subTest(boundary=boundary):
                    data, expected, *_ = fixture(boundary=boundary)
                    if boundary is True: data[:4] = b'PIRS'
                    source = root / str(boundary); source.write_bytes(data)
                    output = root / (str(boundary) + '-extracted')
                    self.assertEqual(self.extract(source, output).returncode, 0)
                    self.assertEqual((output / 'module/sample.bin').read_bytes(), expected)
                    self.assertEqual(source.read_bytes(), data)
                    self.assertFalse(list(root.glob('*.partial-*')))

    def test_rejects_invalid_input_before_output_creation(self):
        with tempfile.TemporaryDirectory(prefix='evac-xbla-invalid-') as temporary:
            root = Path(temporary)
            for case in ('hash', 'cycle', 'type', 'magic', 'path', 'parent', 'short'):
                with self.subTest(case=case):
                    data, _, put, rehash, _, entry = fixture()
                    if case == 'hash': data[-4096] ^= 1
                    if case == 'cycle': rehash(1, 1)
                    if case == 'type': put(0x344, 2)
                    if case == 'magic': data[:4] = b'CON '
                    if case == 'path':
                        data[entry:entry+40] = bytes(40); data[entry:entry+3] = b'../'; data[entry+40] = 3; rehash(0)
                    if case == 'parent': put(entry+50, 1, 2); rehash(0)
                    if case == 'short': del data[-1]
                    source = root / case; source.write_bytes(data); output = root / (case + '-out')
                    self.assertNotEqual(self.extract(source, output).returncode, 0)
                    self.assertFalse(output.exists()); self.assertFalse(list(root.glob('*.partial-*')))
                    self.assertEqual(source.read_bytes(), data)

    def test_existing_directory_and_link_are_not_overwritten(self):
        with tempfile.TemporaryDirectory(prefix='evac-xbla-existing-') as temporary:
            root = Path(temporary)
            data, *_ = fixture(); source = root / 'package'; source.write_bytes(data)
            output = root / 'existing'; output.mkdir(); sentinel = output / 'keep'; sentinel.write_text('keep')
            self.assertNotEqual(self.extract(source, output).returncode, 0)
            self.assertEqual(sentinel.read_text(), 'keep')
            self.assertEqual(list(output.iterdir()), [sentinel])
            missing_parent = root / 'missing/out'
            self.assertNotEqual(self.extract(source, missing_parent).returncode, 0)
            self.assertFalse(missing_parent.parent.exists())
            link = root / 'link'
            result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(output)], capture_output=True)
            if result.returncode:
                self.skipTest('Cannot create authored junction fixture')
            try:
                self.assertNotEqual(self.extract(source, link / 'out').returncode, 0)
                self.assertFalse((output / 'out').exists())
            finally:
                os.rmdir(link)


if __name__ == '__main__':
    unittest.main()
