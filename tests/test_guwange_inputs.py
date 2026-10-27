"""Guwange's checked-in inputs are version-bound metadata, not game content."""
import json
from pathlib import Path
import sys
import tempfile
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_game import verify_data, targets, registry_source
from generate_catalogue import load


class GuwangeInputTests(unittest.TestCase):
    def setUp(self):
        self.root = ROOT / 'games/guwange'
        self.inputs = json.loads((self.root / 'inputs.json').read_text(encoding='utf-8'))
        self.data = json.loads((self.root / 'data.json').read_text(encoding='utf-8'))
        self.manifest = tomllib.loads((self.root / 'recomp/guwange.toml').read_text(encoding='utf-8'))

    def test_base_manifest_matches_five_xex_programs(self):
        self.assertEqual(self.data['variant'], 'xbla-base')
        self.assertFalse(self.data['titleUpdateApplied'])
        programs = {i['path']: i for i in self.data['files'] if i['program']}
        self.assertEqual(len(programs), 5)
        self.assertEqual(len(self.data['files']), 29)
        self.assertEqual({i['path'] for i in self.inputs['xex']}, programs.keys())
        for header in self.inputs['xex']:
            self.assertEqual(header['titleId'], '58410A78')
            self.assertEqual(header['versionRaw'], '0x00000004')
            for key in ('bytes', 'sha256'):
                self.assertEqual(header[key], programs[header['path']][key])
        self.assertEqual({i['imageBase'] for i in self.inputs['xex'] if i['path'] != 'default.xex'},
                         {'0x88000000'})

    def test_each_mode_has_a_separate_output_and_hash_bound_declaration(self):
        records = [self.manifest['entrypoint'], *self.manifest['modules']]
        outputs = set()
        for record in records:
            program = record.get('guest_path', 'default.xex')
            header = next(i for i in self.inputs['xex'] if i['path'] == program)
            name = Path(program).stem
            self.assertEqual(record['file_path'], '../../../game-data/guwange/' + program)
            self.assertEqual(record['out_directory_path'], '../../../generated/guwange/' + name)
            outputs.add(record['out_directory_path'])
            self.assertEqual(len(record['includes']), 7 if name == 'default' else 2)
            self.assertEqual('native-wait-overrides.toml' in record['includes'], name == 'default')
            declaration = (self.root / 'recomp' / record['includes'][0]).read_text(encoding='utf-8')
            self.assertIn(header['sha256'], declaration)
            functions = tomllib.loads(declaration)['functions']
            self.assertEqual(len(functions), 1)
            self.assertEqual(next(iter(functions.values()))['size'], 92 if name == 'default' else 36)
        self.assertEqual(len(outputs), 5)

    def test_boot_observations_preserve_original_control_flow(self):
        hooks = tomllib.loads((self.root / 'recomp/boot-observations.toml').read_text())['midasm_hook']
        self.assertEqual({item['address'] for item in hooks}, {
            0x821E97A8, 0x821E97B4, 0x82258FD0, 0x82258FEC, 0x82259028, 0x8225906C,
            0x821E982C, 0x821E9864, 0x821E989C, 0x821E98A0, 0x821E98AC, 0x8220DA60})
        for item in hooks:
            self.assertTrue(item['registers'])
            self.assertTrue(all(register.startswith('r') for register in item['registers']))
            self.assertNotIn('return', item)
            self.assertNotIn('skip', item)

    def test_base_crt_and_callbacks_have_version_bound_declarations(self):
        declarations = tomllib.loads((self.root / 'recomp/default-functions.toml').read_text())
        self.assertEqual(declarations['setjmp_address'], 0x823EB8C0)
        self.assertEqual(declarations['longjmp_address'], 0x823EB5A0)
        text = (self.root / 'recomp/default-callbacks.toml').read_text()
        self.assertIn(self.inputs['xex'][0]['sha256'], text)
        callbacks = tomllib.loads(text)['functions']
        self.assertEqual(callbacks['0x824F4180']['size'], 0x88)
        self.assertEqual(callbacks['0x821C86C8']['size'], 48)
        self.assertNotIn('0x821C01A8', callbacks)  # SEH continuation, not a function.
        self.assertEqual(text.count('bytes SHA-256:'), len(callbacks))

    def test_main_lifetime_hook_is_base_bound_and_keeps_original_branch(self):
        text = (self.root / 'recomp/main-wait.toml').read_text()
        self.assertIn(self.inputs['xex'][0]['sha256'], text)
        hooks = tomllib.loads(text)['midasm_hook']
        self.assertEqual(hooks, [dict(address=0x821E98A4,
            name='GuwangeMainThreadWait', registers=['r11'])])

    def test_native_community_api_hooks_are_base_bound_and_linked(self):
        text = (self.root / 'recomp/community-overrides.toml').read_text()
        self.assertIn(self.inputs['xex'][0]['sha256'], text)
        self.assertIn('community-overrides.toml', self.manifest['entrypoint']['includes'])
        self.assertIn('community/guest_hooks.cpp', (self.root / 'CMakeLists.txt').read_text())
        expected = {
            0x8224FFA0: ('GuwangeCommunityEstimateRank', 7),
            0x8224FD10: ('GuwangeStorageUpload', 7),
            0x8224FA30: ('GuwangeStorageDownload', 9),
            0x82250180: ('GuwangeStorageLocalPath', 9),
            0x82250218: ('GuwangeStorageRemotePath', 9),
        }
        hooks = tomllib.loads(text)['midasm_hook']
        self.assertEqual({h['address'] for h in hooks}, expected.keys())
        implementation = (self.root / 'community/guest_hooks.cpp').read_text()
        for hook in hooks:
            name, last = expected[hook['address']]
            self.assertEqual(hook, dict(address=hook['address'], name=name,
                registers=[f'r{i}' for i in range(3, last + 1)], **{'return': True}))
            self.assertIn('void ' + name + '(', implementation)
        self.assertEqual(implementation.count('StartCommunityRankIo('), 1)
        self.assertEqual(implementation.count('StartCommunityReplayIo('), 2)
        self.assertEqual(implementation.count('BuildCommunityReplayPath('), 2)
        # No replacement of the original score/replay producer or rank gate.
        self.assertNotIn(0x821D6108, expected)

    def test_state_writing_core_callbacks_include_blue_crash_entry(self):
        expected = {
            'Guwange_blue_x360': {0x882C95A0: 0x4C, 0x8838F1F0: 0x3C, 0x885F8D70: 0x80},
            'Guwange_arrange_x360': {0x8823E268: 0x58, 0x882644F0: 0x9C,
                0x883A8240: 0x50, 0x8860A350: 0x54},
            'Guwange01_coop_x360': {0x8813B7F8: 0x74, 0x8813DD98: 0x48,
                0x88251E10: 0x48, 0x88252C78: 0x34, 0x8835E3E0: 0x48,
                0x885256F0: 0x48, 0x88536A78: 0x48, 0x88536C10: 0x74,
                0x8855CD88: 0x90, 0x885661A0: 0x4C},
        }
        excluded_continuations = {'Guwange_blue_x360': '0x8860D924',
            'Guwange_arrange_x360': '0x8861F304', 'Guwange01_coop_x360': '0x88610C14'}
        for name, entries in expected.items():
            text = (self.root / 'recomp' / (name + '-callbacks.toml')).read_text()
            header = next(i for i in self.inputs['xex']
                          if i['path'] == 'module/' + name + '.dll')
            self.assertIn(header['sha256'], text)
            functions = tomllib.loads(text)['functions']
            self.assertEqual(text.count('bytes SHA-256:'), len(functions))
            for address, size in entries.items():
                self.assertEqual(functions[f'0x{address:08X}'], dict(size=size))
            self.assertNotIn(excluded_continuations[name], functions)

    def test_xbla_edition_and_texture_defaults_are_title_scoped(self):
        host = (self.root / 'host.cpp').read_text()
        self.assertIn('SetFlagByName("license_mask", "1")', host)
        self.assertIn('SetFlagByName("gpu_allow_invalid_fetch_constants", "true")', host)
        shared = (ROOT / 'backends/xbox360/host.cpp').read_text()
        self.assertNotIn('license_mask', shared)
        self.assertNotIn('gpu_allow_invalid_fetch_constants', shared)

    def test_base_rejects_update_companions_before_reading_resources(self):
        forbidden = self.data['forbiddenFiles']
        self.assertEqual(set(forbidden), {i['path'] + 'p' for i in self.inputs['xex']})
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in forbidden:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'authored patch placeholder')
                with self.assertRaisesRegex(ValueError, 'Unsupported companion'):
                    verify_data(dict(files=[], forbiddenFiles=forbidden), root)
                self.assertEqual(path.read_bytes(), b'authored patch placeholder')
                path.unlink()

    def test_integrated_build_covers_native_community_and_nested_xex_dlls(self):
        game = next(g for g in load(ROOT / 'games/catalogue.json') if g['id'] == 'guwange')
        self.assertTrue(game['communitySupported'])
        self.assertTrue(game['integrated'])
        self.assertFalse(game['comingSoon'])
        records = targets(self.manifest, game)
        self.assertEqual(len(records), 5)
        source = registry_source(records, 'guwange')
        for name, item in records.items():
            if name != 'default':
                self.assertIn('module/' + name + '.dll', source)
                self.assertIn('guwange_' + name + '.dll', source)
        self.assertEqual(source.count('RegisterRecompiledModule('), 4)


if __name__ == '__main__':
    unittest.main()
