"""Exercise the shipped ISO extraction core with authored XDVDFS fixtures."""
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'out/common/bin/Extract-Xbox360.exe'
CANCEL = ROOT / 'out/common/bin/xbox360_extract_cancel_test.exe'
SECTOR = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'


def directory(entries):
    out = bytearray()
    positions = []
    for name, sector, size, attr in entries:
        name = name.encode('utf-8')
        positions.append(len(out))
        out += struct.pack('<HHIIBB', 0, 0, sector, size, attr, len(name)) + name
        out += b'\0' * (-len(out) % 4)
    for a, b in zip(positions, positions[1:]):
        struct.pack_into('<H', out, a+2, b//4)
    return out


def iso(path, partition=0, filename='default.xex', cycle=False, truncate=False, directory_cycle=False):
    payload = b'authored executable fixture'
    root = directory([(filename, 50, len(payload), 0), ('media', 41, 64, 16)])
    child = directory([('large.bin', 64, 3*1024*1024, 0), ('empty', 0, 0, 16)])
    if cycle:
        struct.pack_into('<H', root, 2, 1)
    if directory_cycle:
        struct.pack_into('<II', root, root.find(b'media')-10, 40, len(root))
    volume = bytearray(SECTOR)
    volume[:len(MAGIC)] = MAGIC
    volume[-len(MAGIC):] = MAGIC
    struct.pack_into('<II', volume, 20, 40, len(root))
    with path.open('wb') as f:
        for offset, data in ((32*SECTOR, volume), (40*SECTOR, root), (41*SECTOR, child), (50*SECTOR, payload)):
            f.seek(partition+offset); f.write(data)
        f.seek(partition+64*SECTOR); f.write(bytes(range(256)) * (3*1024*1024//256))
        if truncate:
            f.truncate(partition+64*SECTOR+10)
    return payload


@unittest.skipUnless(EXE.is_file() and CANCEL.is_file(), 'Build xbox360_extract and xbox360_extract_cancel_test')
class XboxDiscExtractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='evac-xiso-')
        self.root = Path(self.temp.name)
        self.input = self.root / '普通 ISO with spaces.iso'
        self.output = self.root / '提取 output'

    def tearDown(self):
        self.temp.cleanup()

    def run_extract(self):
        return subprocess.run([str(EXE), '--extract', '--input', str(self.input), '--output', str(self.output)], timeout=30).returncode

    def test_xiso_and_full_disc_partitions_unicode_and_file_contents(self):
        for partition in (0, 0xfd90000, 0x2080000, 0x18300000):
            with self.subTest(partition=partition):
                self.output = self.root / f'中文 output {partition}'
                payload = iso(self.input, partition)
                self.assertEqual(self.run_extract(), 0)
                self.assertEqual((self.output/'default.xex').read_bytes(), payload)
                expected = hashlib.sha256(bytes(range(256))*(3*1024*1024//256)).hexdigest()
                self.assertEqual(hashlib.sha256((self.output/'media/large.bin').read_bytes()).hexdigest(), expected)
                self.assertTrue((self.output/'media/empty').is_dir())

    def test_bad_paths_cycles_truncated_disc_and_non360_are_not_published(self):
        cases = [dict(filename=n) for n in ('../escape', 'CON.txt', 'file:stream', 'trailing.', 'a\\b', 'default.xbe')]
        cases += [dict(cycle=True), dict(truncate=True), dict(directory_cycle=True)]
        for options in cases:
            with self.subTest(options=options):
                iso(self.input, **options)
                self.assertNotEqual(self.run_extract(), 0)
                self.assertFalse(self.output.exists())
                self.assertFalse(list(self.root.glob('.xbox360-extract-*')))

    def test_existing_directory_is_not_modified(self):
        iso(self.input)
        self.output.mkdir(); (self.output/'keep.txt').write_text('existing')
        self.assertNotEqual(self.run_extract(), 0)
        self.assertEqual(list(self.output.iterdir()), [self.output/'keep.txt'])
        self.assertEqual((self.output/'keep.txt').read_text(), 'existing')

    def test_cancel_mid_file_and_destination_created_during_extraction(self):
        for mode in ('cancel', 'destination-race'):
            with self.subTest(mode=mode):
                self.output = self.root / mode
                iso(self.input)
                result = subprocess.run([str(CANCEL), mode, str(self.input), str(self.output)], capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertFalse(list(self.root.glob('.xbox360-extract-*')))
                if mode == 'cancel':
                    self.assertFalse(self.output.exists())
                else:
                    self.assertEqual((self.output/'keep.txt').read_text(), 'existing data')
                    self.assertEqual(len(list(self.output.iterdir())), 1)


if __name__ == '__main__':
    unittest.main()
