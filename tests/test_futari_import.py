from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import import_futari_sources as module


class FutariImportTests(unittest.TestCase):
    def fixture(self, root):
        source = root / 'reference'
        source.mkdir()
        subprocess.run(['git', 'init', '-q', str(source)], check=True)
        subprocess.run(['git', '-C', str(source), 'config', 'core.autocrlf', 'false'], check=True)
        for name in ('project-v0.9/src/audio_wait_policy.cpp',
                     'project-v0.9/src/player_settings.cpp',
                     'project-v0.9/mushihimesama_futari_manifest.toml',
                     'sdk-source/src/audio.cpp', 'game-data/default.xex'):
            target = source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('// fixture\n', encoding='utf-8')
        subprocess.run(['git', '-C', str(source), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(source), '-c', 'user.name=Fixture',
                        '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        commit = module.git(source, 'rev-parse', 'HEAD').decode().strip()
        return source, commit

    def test_selection_excludes_game_bodies_settings_and_sdk(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, commit = self.fixture(root)
            collection = root / 'collection'
            report = collection / '.local/import.json'
            with patch.object(module, 'BASELINE', commit):
                result = module.migrate(source, report, collection)
            self.assertEqual(result['files'], 2)
            self.assertTrue((collection / 'games/futari/audio/audio_wait_policy.cpp').is_file())
            self.assertTrue((collection / 'games/futari/recomp/futari.toml').is_file())
            self.assertFalse((collection / 'game-data').exists())
            self.assertFalse((collection / 'sdk-source').exists())
            self.assertFalse((collection / 'games/futari/player_settings.cpp').exists())
            self.assertEqual(module.git(source, 'status', '--porcelain'), b'')

    def test_existing_target_and_dirty_reference_are_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, commit = self.fixture(root)
            collection = root / 'collection'
            target = collection / 'games/futari/audio/audio_wait_policy.cpp'
            target.parent.mkdir(parents=True)
            target.write_text('existing', encoding='utf-8')
            with patch.object(module, 'BASELINE', commit):
                with self.assertRaises(FileExistsError):
                    module.migrate(source, collection / '.local/one.json', collection)
                (source / 'project-v0.9/src/audio_wait_policy.cpp').write_text('dirty', encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'uncommitted'):
                    module.migrate(source, collection / '.local/two.json', collection)
            self.assertEqual(target.read_text(), 'existing')
            self.assertFalse((collection / '.local').exists())

    def test_import_mapping_is_narrow(self):
        for name in ('game-data/default.xex', 'project-v0.9/generated/a.cpp',
                     'project-v0.9/src/main.cpp', 'project-v0.9/src/mushihimesama_futari_app.h',
                     'project-v0.9/src/player_settings.h', 'sdk-source/a.cpp'):
            self.assertIsNone(module.destination(name))


if __name__ == '__main__':
    unittest.main()
