"""External keyboard configuration; no game data or interactive input required."""
import hashlib
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_native import ROOT, exit_fixture, synthetic_probe

EXE = ROOT / 'out/native/KetsuiNativeProbe.exe'


class InputConfigTests(unittest.TestCase):
    def check_config(self, data):
        with tempfile.TemporaryDirectory(prefix='kt-input-') as directory:
            path = Path(directory) / '按键 设置.ini'
            path.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)
            result = subprocess.run([str(EXE), '--input-config-check', str(path)], capture_output=True,
                                    text=True, encoding='utf-8', timeout=5)
            self.assertEqual(path.read_bytes(), data.encode('utf-8') if isinstance(data, str) else data)
            return result

    def bindings(self, result, expected_exit=0):
        self.assertEqual(result.returncode, expected_exit, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        return {e['virtual_key']: e['button'] for e in events if e['event'] == 'native_pad_keyboard_binding'}

    def test_shipped_template_preserves_all_previous_defaults(self):
        actual = self.bindings(self.check_config((ROOT / 'config/input.ini').read_bytes()))
        expected = {38: 0x10, 39: 0x20, 40: 0x40, 37: 0x80,
                    ord('Z'): 0x4000, ord('X'): 0x2000, ord('C'): 0x8000, ord('V'): 0x1000,
                    13: 8, 8: 0x100000, ord('A'): 0x400, ord('S'): 0x800, ord('Q'): 0x100, ord('W'): 0x200}
        self.assertEqual(actual, expected)
        self.assertEqual(self.bindings(self.check_config('')), expected)

    def test_partial_bom_crlf_comments_aliases_and_disable(self):
        actual = self.bindings(self.check_config('\ufeff; 中文注释\r\n[KEYBOARD]\r\nSQUARE = Space, b # alias\r\nl1 = NONE\r\n'))
        self.assertEqual(actual[32], 0x8000)
        self.assertEqual(actual[ord('B')], 0x8000)
        self.assertEqual(actual[ord('Z')], 0x4000)
        self.assertNotIn(ord('C'), actual)
        self.assertNotIn(ord('A'), actual)

    def test_shared_community_section_in_either_order_preserves_keyboard_bindings(self):
        community = '[community]\nendpoint=https://xiaolu.life/ketsui/api/v1\nnickname=大佐候补生\nenabled=true\n'
        keyboard = '[keyboard]\nsquare=Space\n'
        for content in (community + keyboard, keyboard + community):
            with self.subTest(content=content):
                result = self.check_config(content)
                self.assertEqual(self.bindings(result)[32], 0x8000)
                self.assertNotIn('大佐候补生', result.stdout + result.stderr)
                self.assertNotIn('native_community_request', result.stdout)

    def test_bad_community_config_rejects_whole_file_before_bindings_are_published(self):
        invalid = ('enabled=1\nnickname=fixture', 'enabled=true\nnickname=',
                   'enabled=true\nsecret=fixture', 'enabled=true\nenabled=false',
                   'enabled=true\nendpoint=http://example.invalid/ketsui/api/v1',
                   'enabled=true\nendpoint=https://xiaolu.life/ketsui/api/v1#fragment')
        for fields in invalid:
            with self.subTest(fields=fields):
                result = self.check_config('[keyboard]\nsquare=Space\n[community]\n' + fields)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn('native_pad_keyboard_binding', result.stdout)
        repeated = self.check_config('[community]\nenabled=false\n[KEYBOARD]\n[COMMUNITY]\nenabled=false')
        self.assertEqual(repeated.returncode, 2)

    def test_shipped_template_has_only_three_network_options_and_shared_default_nickname(self):
        import configparser
        config = configparser.ConfigParser()
        config.read_string((ROOT / 'config/input.ini').read_text(encoding='utf-8'))
        self.assertEqual(set(config['community']), {'endpoint', 'nickname', 'enabled'})
        self.assertEqual(config['community']['endpoint'], 'https://xiaolu.life/ketsui/api/v1')
        self.assertEqual(config['community']['nickname'], 'Colonel-404')
        self.assertTrue(config['community']['nickname'].isascii())
        self.assertLessEqual(len(config['community']['nickname']), 16)
        self.assertTrue(config['community'].getboolean('enabled'))

    def test_swaps_are_order_independent_and_support_eight_aliases(self):
        for text in ('cross=X\ncircle=Z', 'circle=Z\ncross=X'):
            actual = self.bindings(self.check_config('[keyboard]\n' + text))
            self.assertEqual(actual[ord('X')], 0x4000)
            self.assertEqual(actual[ord('Z')], 0x2000)
        actual = self.bindings(self.check_config('[keyboard]\nsquare=0,1,2,3,4,5,6,7\n'))
        self.assertTrue(all(actual[ord(str(i))] == 0x8000 for i in range(8)))

    def test_named_keys_and_disabled_thumb_buttons(self):
        names = {'Space': 32, 'Escape': 27, 'Tab': 9, 'Home': 36, 'End': 35,
                 'PageUp': 33, 'PageDown': 34, 'Insert': 45, 'Delete': 46, 'Shift': 16, 'Ctrl': 17,
                 'Numpad0': 96, 'Numpad9': 105, 'Multiply': 106, 'Add': 107,
                 'Subtract': 109, 'Decimal': 110, 'Divide': 111, 'F1': 112, 'F9': 120, 'F12': 123, 'F24': 135}
        for name, key in names.items():
            with self.subTest(name=name):
                self.assertEqual(self.bindings(self.check_config('[keyboard]\nl3=' + name))[key], 2)
        self.assertEqual(self.bindings(self.check_config('[keyboard]\nr3=9'))[ord('9')], 4)

    def test_invalid_sections_buttons_keys_duplicates_and_conflicts(self):
        invalid = ('square=C', '[keys]\nsquare=C', '[keyboard]\n[keyboard]',
                   '[keyboard]\nshot=C', '[keyboard]\nsquare', '[keyboard]\nsquare=',
                   '[keyboard]\nsquare=Space\nSQUARE=B', '[keyboard]\nsquare=Space,space',
                   '[keyboard]\nsquare=Space,', '[keyboard]\nsquare=none,B',
                   '[keyboard]\nsquare=Ctrl+C', '[keyboard]\nsquare=F11', '[keyboard]\nsquare=F10',
                   '[keyboard]\nsquare=F0', '[keyboard]\nsquare=F25', '[keyboard]\nsquare=F01',
                   '[keyboard]\nsquare=0,1,2,3,4,5,6,7,8', '[keyboard]\nsquare=X',
                   '[keyboard]\nsquare=Space\ncircle=Space', '[keyboard]\nsquare=不存在')
        for text in invalid:
            with self.subTest(text=text):
                result = self.check_config(text)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn('Input config line ', result.stderr)
                self.assertNotIn('native_pad_keyboard_binding', result.stdout)

    def test_limits_utf16_nul_and_missing_file(self):
        for data in (b'#' * 65537, b'#' * 1025, '[keyboard]\nsquare=C'.encode('utf-16'), b'[keyboard]\n#\0'):
            with self.subTest(prefix=data[:25]):
                result = self.check_config(data)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        with tempfile.TemporaryDirectory() as directory:
            for path in (Path(directory) / 'missing.ini', Path(directory)):
                result = subprocess.run([str(EXE), '--input-config-check', str(path)], capture_output=True,
                                        text=True, timeout=5)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_entry_applies_config_before_game_and_validation_modes_reject_it(self):
        with tempfile.TemporaryDirectory() as directory:
            image, config = Path(directory) / 'test.ktprobe', Path(directory) / 'input.ini'
            image.write_bytes(exit_fixture())
            config.write_text('[keyboard]\nsquare=Space\n', encoding='utf-8')
            command = [str(EXE), str(image), '--enter', '--input-config', str(config)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            self.assertEqual(self.bindings(result, expected_exit=7)[32], 0x8000)
            for mode in ('--validate', '--binding-census', '--trace-enter'):
                result = subprocess.run([str(EXE), str(image), mode, '--input-config', str(config)],
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn('enter_game', result.stdout)
            result = subprocess.run(command + ['--input-config', str(config)], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            config.write_text('[keyboard]\nsquare=X\n', encoding='utf-8')
            result = subprocess.run(command, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn('image_prepared', result.stdout)
            self.assertNotIn('enter_game', result.stdout)

    def test_diagnostic_hold_cannot_silently_override_custom_h(self):
        with tempfile.TemporaryDirectory() as directory:
            image, config = Path(directory) / 'test.ktprobe', Path(directory) / 'input.ini'
            image.write_bytes(exit_fixture())
            config.write_text('[keyboard]\nsquare=H\n', encoding='utf-8')
            result = subprocess.run([str(EXE), str(image), '--enter', '--input-config', str(config),
                                     '--diagnostic-circle-hold-ms', '1500'], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn('H is already bound', result.stderr)
            self.assertNotIn('enter_game', result.stdout)

    def test_direct_exe_reads_adjacent_config_not_working_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            portable, other = root / 'portable', root / 'cwd'
            portable.mkdir()
            other.mkdir()
            exe = portable / EXE.name
            shutil.copy2(EXE, exe)
            image = root / 'test.ktprobe'
            image.write_bytes(exit_fixture())
            (other / 'input.ini').write_text('[keyboard]\nsquare=X', encoding='utf-8')
            command = [str(exe), str(image), '--enter']
            missing = subprocess.run(command, cwd=other, capture_output=True, text=True, timeout=5)
            self.assertEqual(missing.returncode, 7, missing.stdout + missing.stderr)
            self.assertIn('"using_defaults":true', missing.stdout)
            config = portable / 'input.ini'
            config.write_text('[keyboard]\nsquare=Space', encoding='utf-8')
            before = config.read_bytes()
            result = subprocess.run(command, cwd=other, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            self.assertIn('"virtual_key":32,"button":32768', result.stdout)
            self.assertEqual(config.read_bytes(), before)
            config.write_text('[keyboard]\nsquare=X', encoding='utf-8')
            malformed = subprocess.run(command, cwd=other, capture_output=True, text=True, timeout=5)
            self.assertEqual(malformed.returncode, 2, malformed.stdout + malformed.stderr)
            self.assertNotIn('enter_game', malformed.stdout)
            self.assertNotIn('"using_defaults":true', malformed.stdout)
            explicit = root / 'explicit.ini'
            explicit.write_text('[keyboard]\nsquare=Space', encoding='utf-8')
            override = subprocess.run(command + ['--input-config', str(explicit)], cwd=other,
                                      capture_output=True, text=True, timeout=5)
            self.assertEqual(override.returncode, 7, override.stdout + override.stderr)
            self.assertIn('"virtual_key":32,"button":32768', override.stdout)

    @unittest.skipUnless(shutil.which('pwsh'), 'PowerShell 7 required')
    def test_run_script_archives_editable_config_independently_of_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image, source, config = root / 'test.ktprobe', root / 'source.bin', root / 'custom.ini'
            data = synthetic_probe()
            image.write_bytes(data)
            source.write_bytes(b'Authored synthetic source, no game data')
            image.with_suffix('.json').write_text(json.dumps({'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'probe_sha256': hashlib.sha256(data).hexdigest(), 'contracts': []}), encoding='utf-8')
            config.write_text('[keyboard]\nsquare=Space', encoding='utf-8')
            original = config.read_bytes()
            result = subprocess.run([shutil.which('pwsh'), '-NoProfile', '-File', str(ROOT / 'scripts/Run-NativeProbe.ps1'),
                '-InputFile', str(image), '-SourceFile', str(source), '-ReportRoot', str(root / 'reports'),
                '-UserDirectory', str(root / 'user'), '-InputConfig', str(config), '-Enter'],
                capture_output=True, text=True, timeout=25)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            reports = list((root / 'reports').glob('native-*'))
            self.assertEqual(len(reports), 1)
            record = json.loads((reports[0] / 'run.json').read_text(encoding='utf-8-sig'))
            self.assertEqual(record['exitCode'], 20)
            self.assertEqual(record['inputConfig']['source'], str(config))
            snapshot = Path(record['inputConfig']['snapshot'])
            self.assertEqual(snapshot.read_bytes(), original)
            self.assertEqual(record['inputConfig']['sha256'], hashlib.sha256(original).hexdigest())
            self.assertEqual(config.read_bytes(), original)
            self.assertIn('"virtual_key":32,"button":32768', (reports[0] / 'stdout.jsonl').read_text())


if __name__ == '__main__':
    unittest.main()
