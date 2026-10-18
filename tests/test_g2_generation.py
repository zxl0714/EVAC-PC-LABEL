import copy
import importlib.util
import json
from pathlib import Path
import sys
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import generate_game as generate
from generate_catalogue import load


class GenerationMetadataTests(unittest.TestCase):
    def setUp(self):
        self.manifest = tomllib.loads((ROOT / 'games/espgaluda2/recomp/espgaluda2.toml').read_text(encoding='utf-8'))
        self.lock = next(game for game in load(ROOT / 'games/catalogue.json') if game['id'] == 'espgaluda2')

    def test_covers_eight_modules(self):
        records = generate.targets(self.manifest, self.lock)
        self.assertEqual(len(records), 8)
        self.assertEqual(next(iter(records)), 'default')

    def test_unsafe_or_duplicate_module_is_rejected(self):
        for guest in ('../dat00.xex', 'sub/dat00.xex', 'dat00.xex', 'evil.xex'):
            item = copy.deepcopy(self.manifest)
            item['modules'][1]['guest_path'] = guest
            with self.subTest(guest=guest), self.assertRaises(ValueError):
                generate.targets(item, self.lock)

    def test_output_and_inputs_are_fixed(self):
        for key, value in (('out_directory_path', '../game-data/dat00'), ('file_path', '../../default.xex')):
            item = copy.deepcopy(self.manifest)
            item['modules'][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                generate.targets(item, self.lock)

    def test_missing_module_is_rejected(self):
        self.manifest['modules'].pop()
        with self.assertRaises(ValueError):
            generate.targets(self.manifest, self.lock)

    def test_registry_contains_all_overlays_once(self):
        records = generate.targets(self.manifest, self.lock)
        source = generate.registry_source(records, self.lock['id'])
        self.assertEqual(source.count('RegisterRecompiledModule('), 7)
        for name in records:
            if name != 'default':
                self.assertEqual(source.count('"espgaluda2_' + name + '.dll"'), 1)
        self.assertNotIn('PPC_FUNC', source)


if __name__ == '__main__':
    unittest.main()
