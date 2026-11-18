"""Exercise the automatic DLC reader with the same authored STFS corpus as the helper."""
import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_xbla_extract import fixture

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'out/common/bin/cave_stfs_tests.exe'

@unittest.skipUnless(EXE.is_file(), 'Build cave_stfs_tests first')
class NativeStfsTests(unittest.TestCase):
    def test_launcher_prepares_title_update_once(self):
        package, patch, put, rehash, table, entry = fixture(dlc=True)
        put(0x344, 0xB0000)
        package[table:table+64] = package[entry:entry+64]
        package[entry:entry+64] = bytes(64)
        name = b'default.xexp'
        package[table:table+40] = name.ljust(40, b'\0')
        package[table+40] = len(name)
        put(table+50, 65535, 2)
        rehash(0)
        padded = patch + bytes(16)
        with tempfile.TemporaryDirectory(prefix='evac-native-tu-') as temporary:
            root = Path(temporary) / 'collection'
            root.mkdir()
            data = Path(temporary) / 'game-data'
            data.mkdir()
            source = data / 'title-update'
            source.write_bytes(package)
            (source.parent / 'default.xex').write_bytes(b'abc')
            result = subprocess.run([str(EXE), '--title-update', str(source), str(root), str(len(patch)),
                                     hashlib.sha256(patch).hexdigest(), hashlib.sha256(padded).hexdigest()],
                                    capture_output=True, timeout=25)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            self.assertEqual(source.read_bytes(), package)
            self.assertEqual((source.parent / 'default.xexp').read_bytes(), padded)
            self.assertEqual(list(source.parent.glob('.tu-*')), [])

    def check(self, data, expected, success=True):
        with tempfile.TemporaryDirectory(prefix='evac-native-dlc-') as temporary:
            root = Path(temporary)
            source = root / 'package'
            output = root / 'extracted'
            source.write_bytes(data)
            result = subprocess.run([str(EXE), str(source), str(output), str(len(expected)),
                                     hashlib.sha256(expected).hexdigest()], capture_output=True, timeout=25)
            self.assertEqual(source.read_bytes(), data)
            if success:
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
                self.assertEqual((output / 'module/sample.bin').read_bytes(), expected)
            else:
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_boundaries_and_pirs(self):
        for boundary in (False, True, 'level2'):
            with self.subTest(boundary=boundary):
                data, expected, *_ = fixture(boundary=boundary, dlc=True)
                self.check(data, expected)
        data, expected, *_ = fixture(dlc=True)
        data[:4] = b'PIRS'
        self.check(data, expected)

    def test_empty_file(self):
        data, _, put, rehash, _, entry = fixture(dlc=True)
        put(entry+44, 0, 3, 'little'); put(entry+52, 0); rehash(0)
        self.check(data, b'')

    def test_bad_packages_do_not_extract(self):
        for case in ('hash', 'table-cycle', 'file-cycle', 'reused', 'long-chain', 'truncated', 'layout', 'magic', 'size', 'title', 'type', 'payload'):
            with self.subTest(case=case):
                data, expected, put, rehash, _, entry = fixture(dlc=True)
                if case == 'hash': data[-4096] ^= 1
                elif case == 'table-cycle': rehash(0, 0)
                elif case == 'file-cycle': rehash(1, 1)
                elif case == 'reused': put(entry+47, 0, 3, 'little'); rehash(0)
                elif case == 'long-chain': rehash(2, 1)
                elif case == 'truncated': del data[-1]
                elif case == 'layout': data[0x37B] = 0
                elif case == 'magic': data[:4] = b'CON '
                elif case == 'size': put(entry+52, 5); rehash(0)
                elif case == 'title': put(0x360, 0x58410A78)
                elif case == 'type': put(0x344, 0xD0000)
                elif case == 'payload': data[-4096] ^= 1; rehash(2)
                self.check(data, expected, False)

    def test_unsafe_paths_and_collision(self):
        for name in ('../escape', 'A:B', 'CON.txt', 'file.', 'NUL', 'LPT1', 'a\\b'):
            with self.subTest(name=name):
                data, expected, _, rehash, _, entry = fixture(dlc=True)
                data[entry:entry+40] = bytes(40); data[entry:entry+len(name)] = name.encode()
                data[entry+40] = len(name); rehash(0)
                self.check(data, expected, False)
        data, expected, put, rehash, table, entry = fixture(dlc=True)
        put(entry+50, 1, 2); rehash(0)
        self.check(data, expected, False)
        data, expected, _, rehash, table, entry = fixture(dlc=True)
        data[table+128:table+192] = data[entry:entry+64]
        data[table+128:table+138] = b'SAMPLE.BIN'; rehash(0)
        self.check(data, expected, False)

if __name__ == '__main__':
    unittest.main()
