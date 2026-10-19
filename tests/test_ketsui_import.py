from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import import_ketsui_sources as module


class KetsuiImportTests(unittest.TestCase):
    def fixture(self, root, binary=False):
        source = root / 'reference'
        source.mkdir()
        subprocess.run(['git', 'init', '-q', str(source)], check=True)
        subprocess.run(['git', '-C', str(source), 'config', 'core.autocrlf', 'false'], check=True)
        for name in ('src/native_memory.cpp', 'src/launcher/launcher.cpp',
                     'third_party/gpu_core/upstream/shader.frag', 'third_party/gcn/dependencies.json',
                     'scripts/native_elf.py', 'scripts/game_fixes.py', 'scripts/Build-Native.ps1',
                     'server/ketsui_live.py', 'LICENSES/GPL-2.0-or-later.txt',
                     'config/input.ini', 'game-data/eboot.bin', 'tests/test_native.py', 'tests/test_release.py'):
            target = source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('// authored fixture\n', encoding='utf-8')
        if binary:
            (source / 'src/native_memory.cpp').write_bytes(b'MZdisguised executable')
        subprocess.run(['git', '-C', str(source), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(source), '-c', 'user.name=Fixture',
                        '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        return source, module.git(source, 'rev-parse', 'HEAD').decode().strip()

    def test_reorganized_source_and_original_are_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, commit = self.fixture(root)
            target = root / 'collection'
            report = target / '.local/import.json'
            with patch.object(module, 'BASELINE', commit):
                result = module.migrate(source, report, target)
            self.assertEqual(result['files'], 8)
            for path in ('backends/ps4/native_memory.cpp', 'third_party/ps4/gpu_core/upstream/shader.frag',
                         'scripts/ps4/native_elf.py', 'scripts/ketsui/game_fixes.py',
                         'third_party/ps4/LICENSES/GPL-2.0-or-later.txt', 'tests/ketsui/test_native.py'):
                self.assertTrue((target / path).is_file())
            self.assertFalse((target / 'game-data').exists())
            self.assertFalse((target / 'config').exists())
            self.assertFalse((target / 'backends/ps4/launcher').exists())
            self.assertEqual(module.git(source, 'status', '--porcelain'), b'')

    def test_dirty_existing_and_binary_preflight_make_no_partial_import(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, commit = self.fixture(root)
            target = root / 'collection'
            report = target / '.local/import.json'
            with patch.object(module, 'BASELINE', commit):
                (source / 'src/native_memory.cpp').write_text('dirty', encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'uncommitted'):
                    module.migrate(source, report, target)
                self.assertFalse(target.exists())
                subprocess.run(['git', '-C', str(source), 'restore', 'src/native_memory.cpp'], check=True)
                existing = target / 'backends/ps4/native_memory.cpp'
                existing.parent.mkdir(parents=True)
                existing.write_text('keep', encoding='utf-8')
                with self.assertRaises(FileExistsError):
                    module.migrate(source, report, target)
                self.assertEqual(existing.read_text(), 'keep')
                self.assertFalse((target / 'scripts').exists())
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, commit = self.fixture(root, binary=True)
            target = root / 'collection'
            with patch.object(module, 'BASELINE', commit):
                with self.assertRaisesRegex(ValueError, 'text maintenance'):
                    module.migrate(source, target / '.local/report.json', target)
                self.assertFalse(target.exists())

    def test_selection_and_report_boundaries(self):
        for name in ('game-data/eboot.bin', 'prepared/a.cpp', 'src/launcher/launcher.cpp',
                     'src/../game-data/a.cpp', '/src/a.cpp', 'src/a.exe',
                     'tools/Environment.ps1', 'config.ini', 'config/input.ini',
                     'tests/fixtures/media/red-aac-v1.mp4', 'tests/test_tools.py'):
            self.assertIsNone(module.destination(name), name)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, commit = self.fixture(root)
            with patch.object(module, 'BASELINE', commit):
                with self.assertRaisesRegex(ValueError, 'separate'):
                    module.migrate(source, source / '.local/report.json', source)
                with self.assertRaisesRegex(ValueError, 'new report'):
                    module.migrate(source, root / 'outside.json', root / 'collection')


if __name__ == '__main__':
    unittest.main()
