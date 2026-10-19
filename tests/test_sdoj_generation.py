import copy
import json
from pathlib import Path
import sys
import tomllib
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import generate_game
from build_receipt import selected_games
from generate_catalogue import load
from project import ROOT


class SdojGenerationTests(unittest.TestCase):
    def setUp(self):
        self.game = next(game for game in load(ROOT / 'games/catalogue.json') if game['id'] == 'sdoj')
        self.manifest = tomllib.loads((ROOT / 'games/sdoj/recomp/sdoj.toml').read_text(encoding='utf-8'))

    def test_three_xex_images_include_two_named_bin_files(self):
        records = generate_game.targets(self.manifest, self.game)
        self.assertEqual(list(records), ['default', 'CA022100', 'CA022110'])
        headers = json.loads((ROOT / 'games/sdoj/inputs.json').read_text(encoding='utf-8'))['xex']
        self.assertEqual({p['path'] for p in headers}, {p['program_path'] for p in records.values()})
        self.assertEqual({p['titleId'] for p in headers}, {'435A07E7'})
        self.assertEqual({p['imageBase'] for p in headers if p['path'] != 'default.xex'}, {'0x88000000'})

    def test_registry_preserves_bin_names_and_registers_both_isolated_libraries(self):
        records = generate_game.targets(self.manifest, self.game)
        source = generate_game.registry_source(records, 'sdoj')
        self.assertEqual(source.count('RegisterRecompiledModule('), 2)
        for name in ('CA022100', 'CA022110'):
            self.assertIn(f'"{name}.bin","{name}.bin"', source)
            self.assertIn(f'"sdoj_{name}.dll"', source)
            self.assertNotIn(name + '.xex', source)
        self.assertNotIn('PPC_FUNC_IMPL', source)

    def test_incomplete_resource_program_or_redirected_module_is_rejected(self):
        for filename in ('CA022100.xex', 'elsewhere/CA022100.bin', 'PKG000.bin', '../CA022100.bin'):
            bad = copy.deepcopy(self.manifest)
            bad['modules'][0]['guest_path'] = filename
            with self.subTest(filename=filename), self.assertRaises(ValueError):
                generate_game.targets(bad, self.game)
        bad = copy.deepcopy(self.manifest)
        bad['modules'].pop()
        with self.assertRaises(ValueError):
            generate_game.targets(bad, self.game)

    def test_build_selection_is_shared_not_another_sdk(self):
        self.assertEqual(selected_games('sdoj'), ('sdoj',))
        self.assertEqual(selected_games('sdoj,futari,espgaluda2'), ('espgaluda2', 'futari', 'sdoj'))

    def test_reviewed_resident_callbacks_exclude_frame_dependent_tails(self):
        functions = tomllib.loads((ROOT / 'games/sdoj/recomp/default-functions.toml').read_text(encoding='utf-8'))['functions']
        self.assertEqual(len(functions), 42)
        self.assertEqual(functions['0x825B6F48']['size'], 12)
        self.assertEqual(functions['0x823D62C0']['size'], 16)
        self.assertEqual(functions['0x824982C0']['size'], 136)
        self.assertEqual(functions['0x82251C10']['size'], 244)
        self.assertTrue(all(4 <= item['size'] <= 244 and item['size'] % 4 == 0 for item in functions.values()))
        self.assertFalse({'0x822478B0', '0x822643D0', '0x8246932C'} & functions.keys())
        self.assertIn('default-functions.toml', self.manifest['entrypoint']['includes'])


if __name__ == '__main__':
    unittest.main()
