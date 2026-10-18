"""Native import boundaries using authored bytes and real Windows junctions."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

EXE = Path(__file__).resolve().parents[1] / 'out/common/bin/cave_launch_tests.exe'


@unittest.skipUnless(EXE.is_file(), 'Build common native targets first')
class DataImportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cave-import-test-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / '合集 空格'
        self.source = self.base / '原始 数据'
        self.root.mkdir()
        self.source.mkdir()
        (self.source / 'main.bin').write_bytes(b'abc')

    def junction(self, link, target):
        if not shutil.which('pwsh'):
            self.skipTest('PowerShell 7 is needed for a real directory junction')
        quote = lambda path: "'" + str(path).replace("'", "''") + "'"
        command = "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path " + quote(link) + ' -Target ' + quote(target) + ' | Out-Null'
        subprocess.run(['pwsh', '-NoLogo', '-NoProfile', '-NonInteractive', '-Command', command],
                       capture_output=True, text=True, check=True)

    def copy(self, source=None, code=1):
        result = subprocess.run([str(EXE), '--copy-fixture', str(self.root), str(source or self.source)],
                                cwd=self.base, capture_output=True, encoding='utf-8', check=False)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return result

    def test_default_container_junction_is_rejected_even_inside_root(self):
        destination = self.root / 'redirected'
        destination.mkdir()
        self.junction(self.root / 'game-data', destination)
        self.assertIn('junction', self.copy().stderr)
        self.assertEqual(list(destination.iterdir()), [])
        self.assertFalse((self.root / 'config.ini').exists())

    def test_empty_target_junction_cannot_redirect_copy(self):
        (self.root / 'game-data').mkdir()
        destination = self.root / 'game-data/another-title'
        destination.mkdir()
        self.junction(self.root / 'game-data/fixture', destination)
        self.assertIn('junction', self.copy().stderr)
        self.assertEqual(list(destination.iterdir()), [])
        self.assertEqual((self.source / 'main.bin').read_bytes(), b'abc')

    def test_source_child_junction_is_rejected(self):
        outside = self.base / 'outside'
        outside.mkdir()
        (outside / 'sentinel.txt').write_text('keep', encoding='utf-8')
        self.junction(self.source / 'res', outside)
        self.assertIn('junction', self.copy().stderr)
        self.assertEqual((outside / 'sentinel.txt').read_text(encoding='utf-8'), 'keep')
        self.assertFalse((self.root / 'game-data/fixture').exists())

    def test_source_root_alias_is_read_only_and_supported(self):
        link = self.base / 'source alias'
        self.junction(link, self.source)
        self.copy(link, code=0)
        self.assertEqual((self.root / 'game-data/fixture/main.bin').read_bytes(), b'abc')
        self.assertEqual((self.source / 'main.bin').read_bytes(), b'abc')

    def test_windows_path_case_does_not_change_copy_identity(self):
        result = subprocess.run([str(EXE), '--copy-fixture', str(self.root).swapcase(), str(self.source).swapcase()],
                                capture_output=True, encoding='utf-8', timeout=10, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'game-data/fixture/main.bin').read_bytes(), b'abc')


if __name__ == '__main__':
    unittest.main()
