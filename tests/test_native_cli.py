"""Read-only native CLI integration; only authored files, never game execution."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'out/common/bin/cave_inspect.exe'


@unittest.skipUnless(EXE.is_file(), 'Build common native targets first')
class NativeCliTests(unittest.TestCase):
    def run_cli(self, root, *args, cwd=None, code=0):
        result = subprocess.run([str(EXE), '--root', str(root), *args], cwd=cwd,
                                capture_output=True, encoding='utf-8', check=False)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def test_empty_unicode_root_is_read_only_and_independent_of_cwd(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / '合集 空格'; root.mkdir()
            result = self.run_cli(root, cwd=Path(temporary))
            self.assertEqual([r['id'] for r in result['games']], ['futari', 'espgaluda2', 'ketsui', 'sdoj', 'daioujou', 'esprade'])
            self.assertEqual(result['games'][1]['dataState'], 'missing_data')
            self.assertEqual(result['games'][3]['dataState'], 'missing_data')
            self.assertEqual(result['games'][4]['state'], 'missing_data')
            self.assertEqual(result['games'][5]['state'], 'coming_soon')
            self.assertEqual(list(root.iterdir()), [])
            self.assertFalse(result['guestExecution'])

    def test_selected_game_does_not_parse_other_game_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'config.ini').write_text('[game.futari]\ndata_dir=\n[game.espgaluda2]\ndata_dir=外部 数据\n', encoding='utf-8')
            result = self.run_cli(root, '--game', 'espgaluda2')['games'][0]
            self.assertEqual(result['dataState'], 'missing_data')
            self.assertEqual(Path(result['dataDirectory']), root / '外部 数据')
            all_rows = self.run_cli(root)['games']
            self.assertEqual(all_rows[0]['state'], 'invalid_config')
            self.assertEqual(all_rows[1]['dataState'], 'missing_data')

    def test_optional_content_does_not_change_base_data_state(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            row = self.run_cli(root, '--game', 'futari', '--full')['games'][0]
            self.assertEqual(row['dataState'], 'missing_data')
            self.assertEqual([p['id'] for p in row['optionalContent']], ['black-label', 'ver101'])
            self.assertEqual([p['state'] for p in row['optionalContent']], ['missing_data', 'missing_data'])
            self.assertEqual(list(root.iterdir()), [])
            other = self.run_cli(root, '--game', 'espgaluda2')['games'][0]
            self.assertEqual(other['optionalContent'], [])

    def test_linked_optional_package_is_not_exposed(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary); root = base / 'collection'; root.mkdir()
            dlc = base / 'dlc'; parent = dlc / '0000000000000000/435A07D7/00000002'; parent.mkdir(parents=True)
            outside = base / 'outside'; outside.mkdir()
            (outside / 'ver101_key.bin').write_bytes(b'unchanged')
            self.junction(parent / '9893A78CA6062B935BD1F53EA43CFD546B7246DF43', outside)
            (root / 'config.ini').write_text('[game.futari]\ndlc_dir=' + str(dlc) + '\n', encoding='utf-8')
            row = self.run_cli(root, '--game', 'futari', '--full')['games'][0]
            self.assertEqual(row['dataState'], 'missing_data')
            self.assertEqual(row['optionalContent'][1]['state'], 'invalid_path')
            self.assertEqual((outside / 'ver101_key.bin').read_bytes(), b'unchanged')

    def test_invalid_arguments_and_newer_config_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assertIn('error', self.run_cli(root, '--game', 'unknown', code=2))
            self.assertIn('error', self.run_cli(root, '--data-dir', str(root), code=2))
            (root / 'config.ini').write_text('[collection]\nconfig_version=99\n', encoding='utf-8')
            self.assertIn('error', self.run_cli(root, code=2))

    def junction(self, link, target):
        if not shutil.which('pwsh'):
            self.skipTest('PowerShell 7 needed for junction fixture')
        quoted = lambda p: "'" + str(p).replace("'", "''") + "'"
        command = "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path " + quoted(link) + ' -Target ' + quoted(target) + ' | Out-Null'
        subprocess.run(['pwsh', '-NoLogo', '-NoProfile', '-NonInteractive', '-Command', command],
                       capture_output=True, text=True, check=True)

    def test_junction_output_to_data_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'collection'; root.mkdir()
            data = root / 'game-data/espgaluda2'; data.mkdir(parents=True)
            (data / 'untouched.txt').write_text('keep', encoding='utf-8')
            self.junction(root / 'logs', data)
            row = self.run_cli(root, '--game', 'espgaluda2')['games'][0]
            self.assertEqual(row['state'], 'invalid_path')
            self.assertEqual((data / 'untouched.txt').read_text(encoding='utf-8'), 'keep')
            self.assertFalse((data / 'espgaluda2').exists())

    def test_data_root_junction_is_resolved_but_children_cannot_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary); root = base / 'collection'; root.mkdir()
            target = base / 'data'; target.mkdir()
            link = root / 'game-data'; link.mkdir()
            self.junction(link / 'espgaluda2', target)
            row = self.run_cli(root, '--game', 'espgaluda2')['games'][0]
            self.assertEqual(Path(row['dataDirectory']), target)
            self.assertEqual(row['dataState'], 'missing_data')
            outside = base / 'outside'; outside.mkdir()
            self.junction(target / 'res', outside)
            row = self.run_cli(root, '--game', 'espgaluda2')['games'][0]
            self.assertEqual(row['state'], 'invalid_path')


if __name__ == '__main__':
    unittest.main()
