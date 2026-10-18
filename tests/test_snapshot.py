import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    'snapshot_sources', Path(__file__).resolve().parents[1] / 'scripts/snapshot_sources.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SnapshotTests(unittest.TestCase):
    def fixture(self, root):
        repo = root / 'repo'
        repo.mkdir()
        subprocess.run(['git', 'init', '-q', str(repo)], check=True)
        (repo / '.gitignore').write_text('game-data/\n', encoding='utf-8')
        (repo / 'source.txt').write_text('before\n', encoding='utf-8')
        subprocess.run(['git', '-C', str(repo), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(repo), '-c', 'user.name=Fixture',
                        '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        return repo

    def test_includes_dirty_and_untracked_sources_but_not_ignored_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            repo = self.fixture(root)
            (repo / 'source.txt').write_text('after\n', encoding='utf-8')
            (repo / '新增.txt').write_text('new\n', encoding='utf-8')
            (repo / 'game-data').mkdir()
            (repo / 'game-data/private.bin').write_bytes(b'not-source')
            before = module.git(repo, 'status', '--porcelain=v1', '-z')
            result = module.snapshot(repo, root / 'backup')
            self.assertEqual(len(result['files']), 3)
            self.assertEqual((root / 'backup/source/新增.txt').read_text(encoding='utf-8'), 'new\n')
            self.assertFalse((root / 'backup/source/game-data').exists())
            self.assertEqual(before, module.git(repo, 'status', '--porcelain=v1', '-z'))
            self.assertIn(b'after', (root / 'backup/working-tree.patch').read_bytes())

    def test_refuses_existing_or_overlapping_targets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            repo = self.fixture(root)
            for target in (root, repo, repo / 'backup'):
                with self.assertRaises(ValueError):
                    module.snapshot(repo, target)

    def test_verification_detects_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            repo = self.fixture(root)
            module.snapshot(repo, root / 'backup')
            (root / 'backup/source/source.txt').write_text('bad\n', encoding='utf-8')
            with self.assertRaises(ValueError):
                module.verify(root / 'backup')

    def test_rejects_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            for name in ('../outside', '/absolute'):
                with self.assertRaises(ValueError):
                    module.checked_path(root, name)


if __name__ == '__main__':
    unittest.main()
