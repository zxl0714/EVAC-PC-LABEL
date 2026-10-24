"""Initialization helpers use authored headers and resources, never game files."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import xex_inventory as project


def authored_xex():
    data = bytearray(128)
    struct.pack_into('>6I', data, 0, 0x58455832, 1, 128, 0, 0, 4)
    for index, pair in enumerate(((0x00010201, 0x82000000), (0x00010100, 0x82001000),
                                   (0x00040006, 64), (0x000003FF, 88))):
        struct.pack_into('>2I', data, 24 + index * 8, *pair)
    struct.pack_into('>6I', data, 64, 0, 3, 3, 0x12345678, 0, 0)
    struct.pack_into('>IHH', data, 88, 8, 1, 1)
    return data


class XexInventoryTests(unittest.TestCase):
    def fixture(self, path):
        path.mkdir()
        (path / 'default.xex').write_bytes(authored_xex())
        (path / 'res').mkdir()
        (path / 'res/body.dat').write_bytes(bytes(range(256)))
        (path / 'empty').mkdir()

    def test_header_reads_big_endian_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.xex'
            path.write_bytes(authored_xex())
            result = project.xex_metadata(path)
            self.assertEqual(result['titleId'], '12345678')
            self.assertEqual(result['imageBase'], '0x82000000')
            self.assertEqual(result['entryPoint'], '0x82001000')
            self.assertEqual(result['versionRaw'], '0x00000003')
            self.assertEqual((result['encryption'], result['compression']), (1, 1))

    def test_header_rejects_invalid_magic_and_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.xex'
            for data in (b'', b'XEX2', b'not a program' * 10, authored_xex()[:40]):
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    project.xex_metadata(path)

    def test_header_rejects_excessive_count_and_duplicate_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.xex'
            for duplicate in (False, True):
                data = authored_xex()
                struct.pack_into('>I', data, 32 if duplicate else 20,
                                 0x00010201 if duplicate else 4097)
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    project.xex_metadata(path)

    def test_header_rejects_optional_header_outside_table_or_header(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.xex'
            for offset in (0, 24, 120, 0xffffffff):
                data = authored_xex()
                struct.pack_into('>I', data, 44, offset)
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    project.xex_metadata(path)

    def test_header_rejects_bad_format_info_size(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.xex'
            for size in (0, 7, 0xffffffff):
                data = authored_xex()
                struct.pack_into('>I', data, 88, size)
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    project.xex_metadata(path)

    def test_inventory_is_read_only_and_contains_only_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / '资料 中文'
            self.fixture(root)
            before = {str(path): path.read_bytes() for path in root.rglob('*') if path.is_file()}
            result = project.inventory(root)
            self.assertEqual(result['fileCount'], 2)
            self.assertEqual(result['totalBytes'], 384)
            self.assertEqual(len(result['xex']), 1)
            self.assertNotIn(str(root), json.dumps(result))
            after = {str(path): path.read_bytes() for path in root.rglob('*') if path.is_file()}
            self.assertEqual(before, after)

    def test_inventory_requires_main_xex(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'default.xex'):
                project.inventory(Path(directory))

    def test_inventory_finds_xex_dll_and_bin_without_treating_windows_dll_as_xex(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'game'
            self.fixture(root)
            (root / 'module').mkdir()
            for name in ('module/core.dll', 'core.bin'):
                (root / name).write_bytes(authored_xex())
            (root / 'module/windows.dll').write_bytes(b'MZ' + bytes(126))
            before = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
            result = project.inventory(root)
            self.assertEqual({item['path'] for item in result['xex']},
                             {'default.xex', 'module/core.dll', 'core.bin'})
            self.assertEqual(before, {p: p.read_bytes() for p in before})

    def test_inventory_rejects_malformed_xex_in_dll(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'game'
            self.fixture(root)
            (root / 'broken.dll').write_bytes(b'XEX2')
            with self.assertRaises(ValueError):
                project.inventory(root)

    def test_inventory_rejects_junction(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(Path, 'is_junction', return_value=True):
                with self.assertRaisesRegex(ValueError, 'Linked source/output'):
                    project.inventory(Path(directory))



if __name__ == '__main__':
    unittest.main()
