from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from ps4 import inspect_archive as module
import analysis_paths
from test_ps4_inventory import sfo


class Ps4ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name).resolve()
        self.patch = patch.object(analysis_paths, 'ROOT', self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def archive(self, extra=(), *, prefix='Title/'):
        path = self.root / 'input.zip'
        with zipfile.ZipFile(path, 'w') as stream:
            stream.writestr(prefix + 'sce_sys/param.sfo', sfo(dict(TITLE_ID='CUSA00001', APP_VER='01.00', CATEGORY='gd')))
            stream.writestr(prefix + 'eboot.bin', b'authored placeholder, never executed')
            for name, data in extra:
                stream.writestr(name, data)
        return path

    def test_read_only_crc_and_metadata_do_not_claim_support(self):
        source = self.archive()
        before = source.read_bytes()
        result = module.inspect(source)
        self.assertEqual(result['title']['TITLE_ID'], 'CUSA00001')
        self.assertEqual(len(result['files']), 2)
        self.assertTrue(result['crcVerified'])
        self.assertFalse(result['guestExecution'])
        self.assertFalse(result['supportedVersionVerified'])
        self.assertEqual(source.read_bytes(), before)

    def test_new_local_extraction_strips_only_title_prefix(self):
        source = self.archive([('Title/assets/empty.bin', b'')])
        output = self.root / '.local/title'
        result = module.inspect(source, output)
        self.assertEqual((output / 'eboot.bin').read_bytes(), b'authored placeholder, never executed')
        self.assertTrue((output / 'assets/empty.bin').is_file())
        self.assertEqual(result['source']['prefix'], 'Title/')
        with self.assertRaises(FileExistsError):
            module.inspect(source, output)

    def test_archive_without_outer_folder(self):
        result = module.inspect(self.archive(prefix=''))
        self.assertEqual(result['source']['prefix'], '')

    def test_unsafe_paths_collisions_and_host_programs(self):
        for name in ('../escape', 'Title/../escape', 'Title/a:stream', 'Title/nul.txt',
                     'Title/.git/config', 'Title/EBoot.bin', 'other/readme.txt',
                     'Title/tool.exe', 'Title/tool.ps1', 'Title/trailing. '):
            with self.subTest(name=name):
                source = self.archive([(name, b'authored')])
                with self.assertRaises(ValueError):
                    module.inspect(source)

    def test_links_and_file_directory_shadowing(self):
        link = zipfile.ZipInfo('Title/link')
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        for extra in ([(link, b'../../outside')], [('Title/assets', b'file'), ('Title/assets/item', b'file')]):
            source = self.archive(extra)
            with self.assertRaises(ValueError):
                module.inspect(source)

    def test_crc_corruption_is_not_accepted(self):
        source = self.archive()
        raw = source.read_bytes().replace(b'authored placeholder', b'corrupt! placeholder', 1)
        source.write_bytes(raw)
        with self.assertRaises(zipfile.BadZipFile):
            module.inspect(source)

    def test_outputs_cannot_escape_local_or_overwrite_input(self):
        source = self.archive()
        for output in (source, self.root / 'outside', self.root / '.local'):
            with self.subTest(output=output), self.assertRaises(ValueError):
                module.inspect(source, output)

    def test_archive_member_budget_is_enforced_before_extraction(self):
        source = self.archive()
        with patch.object(module, 'MAX_TOTAL_BYTES', 1), self.assertRaises(ValueError):
            module.inspect(source, self.root / '.local/title')
        self.assertFalse((self.root / '.local/title').exists())


if __name__ == '__main__':
    unittest.main()
