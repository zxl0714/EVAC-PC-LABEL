import copy
import json
from pathlib import Path
import sys
import tempfile
import tomllib
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import generate_game as generate
from generate_catalogue import load
from project import ROOT


class FutariGenerationTests(unittest.TestCase):
    def setUp(self):
        self.game = next(game for game in load(ROOT / 'games/catalogue.json') if game['id'] == 'futari')
        self.source = ROOT / 'games/futari/recomp'
        self.manifest = tomllib.loads((self.source / 'futari.toml').read_text(encoding='utf-8'))

    def test_nine_programs_match_base_data_and_headers(self):
        records = generate.targets(self.manifest, self.game)
        self.assertEqual(len(self.game['files']), 52)
        self.assertEqual(len(records), 9)
        self.assertEqual(records['hirow_normal']['program_path'], 'media/Module/hirow_normal.xex')
        headers = json.loads((self.source.parent / 'inputs.json').read_text(encoding='utf-8'))['xex']
        self.assertEqual({item['path'] for item in headers}, {item['program_path'] for item in records.values()})
        self.assertEqual({item['titleId'] for item in headers}, {'435A07D7'})
        self.assertTrue(all(item['sha256'] and 'body' not in item for item in headers))

    def test_external_data_paths_preserve_every_module_include(self):
        with tempfile.TemporaryDirectory(prefix='合集 外部 ') as tmp:
            root = Path(tmp)
            records = generate.targets(self.manifest, self.game)
            execution = tomllib.loads(generate.execution_manifest(
                self.game, records, root / '数据', root / 'generated/futari', self.source))
            self.assertEqual(Path(execution['entrypoint']['file_path']), root / '数据/default.xex')
            self.assertIn(str(self.source / 'default-functions.toml').replace('\\', '/'),
                          execution['entrypoint']['includes'])
            for item in execution['modules']:
                key = Path(item['guest_path']).stem
                self.assertEqual(Path(item['file_path']), root / '数据' / records[key]['program_path'])
                self.assertEqual([Path(path).name for path in item['includes']], records[key]['includes'])
            kiniro = next(item for item in execution['modules'] if 'kiniro_abnormal' in item['guest_path'])
            self.assertIn('audio-kiniro-overrides.toml', [Path(path).name for path in kiniro['includes']])

    def test_registry_is_game_scoped_and_retains_nested_guest_paths(self):
        records = generate.targets(self.manifest, self.game)
        source = generate.registry_source(records, 'futari')
        self.assertEqual(source.count('RegisterRecompiledModule('), 8)
        self.assertIn('games/futari', source)
        self.assertIn('media/module/palm_normal.xex', source)
        self.assertNotIn('espgaluda2', source)
        self.assertNotIn('PPC_FUNC_IMPL', source)

    def test_redirected_paths_duplicates_and_silent_declarations_are_rejected(self):
        for change in ('input', 'guest', 'duplicate', 'inline', 'include'):
            bad = copy.deepcopy(self.manifest)
            if change == 'input':
                bad['modules'][0]['file_path'] = '../../../game-data/espgaluda2/default.xex'
            elif change == 'guest':
                bad['modules'][0]['guest_path'] = 'elsewhere/hirow_abnormal.xex'
            elif change == 'duplicate':
                bad['modules'].append(copy.deepcopy(bad['modules'][0]))
            elif change == 'inline':
                bad['entrypoint']['functions'] = {'0x820F3C20': {}}
            else:
                bad['modules'][0]['includes'] = [{}]
            with self.subTest(change=change), self.assertRaises(ValueError):
                generate.targets(bad, self.game)

    def test_resident_callback_inventory_is_retained(self):
        functions = tomllib.loads((self.source / 'default-functions.toml').read_text(encoding='utf-8'))['functions']
        self.assertEqual(len(functions), 31)
        self.assertTrue({'0x820F3C20', '0x821CB190', '0x8210E770', '0x8210E778', '0x8210E780'} <= functions.keys())


if __name__ == '__main__':
    unittest.main()
