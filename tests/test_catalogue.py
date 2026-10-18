import copy
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import generate_catalogue as catalogue
import prepare_tools
import project


class CatalogueTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(__file__).resolve().parents[1]
        self.path = self.root / 'games/catalogue.json'

    def test_real_catalogue_order_and_states(self):
        games = catalogue.load(self.path)
        self.assertEqual([g['id'] for g in games], ['futari', 'espgaluda2', 'ketsui', 'daioujou', 'esprade'])
        self.assertEqual([g['comingSoon'] for g in games], [False, False, False, True, True])
        # G2 has a migrated native runtime; other backends remain pending.
        self.assertEqual([g['id'] for g in games if g['integrated']], ['espgaluda2'])

    def test_g2_inventory_is_complete_metadata_only(self):
        game = catalogue.load(self.path)[1]
        self.assertEqual(len(game['files']), 155)
        self.assertEqual(sum(f['bytes'] for f in game['files']), 677185607)
        self.assertEqual(sum(f['program'] for f in game['files']), 8)
        self.assertTrue(all(set(f) == {'path', 'bytes', 'sha256', 'program'} for f in game['files']))

    def test_generator_is_deterministic_and_contains_only_metadata(self):
        games = catalogue.load(self.path)
        first = catalogue.generate(games)
        self.assertEqual(first, catalogue.generate(copy.deepcopy(games)))
        self.assertIn('std::array<DataFile,155>', first)
        self.assertNotIn('PPCContext', first)

    def test_invalid_relative_paths(self):
        for path in ('../escape', '/absolute', 'C:/drive', 'a\\b', 'a//b', 'a/./b', 'bad\0path', 'file.', 'bad?path'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                catalogue.relative(path)

    def test_duplicate_ids_and_transformed_identifiers(self):
        raw = catalogue.read_json(self.path)
        for other in ('futari', 'fu-tari'):
            value = copy.deepcopy(raw)
            value['games'][1]['id'] = other
            value['games'][1]['dataManifest'] = None
            if other == 'fu-tari':
                value['games'][0]['id'] = 'fu_tari'
            self.reject(value)

    def reject(self, value):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'catalogue.json'
            path.write_text(json.dumps(value), encoding='utf-8')
            # The temporary catalogue needs the same metadata dependencies;
            # missing fixtures must not mask the identity validation under test.
            for game in catalogue.read_json(self.path)['games']:
                if game['dataManifest']:
                    target = path.parent / game['dataManifest']
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(self.path.parent / game['dataManifest'], target)
            with self.assertRaises((ValueError, KeyError)):
                catalogue.load(path)

    def test_unknown_contract_and_invalid_availability(self):
        raw = catalogue.read_json(self.path)
        for version in (2, True):
            value = copy.deepcopy(raw); value['format'] = version
            self.reject(value)
        value = copy.deepcopy(raw); value['games'][0]['integrated'] = True
        value['games'][0]['runtimeFiles'] = []
        self.reject(value)

    def test_duplicate_json_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bad.json'
            path.write_text('{"format":1,"format":2}', encoding='utf-8')
            with self.assertRaises(ValueError):
                catalogue.read_json(path)


class BoundaryTests(unittest.TestCase):
    def test_tool_archive_path_safety(self):
        for path in ('../outside', '/absolute', 'C:/drive', 'folder\\escape'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                prepare_tools.validate_archive_member(path)

    def test_source_audit_current_tree(self):
        result = project.audit_source()
        self.assertFalse(result['gameDataIncluded'])
        self.assertGreater(result['files'], 15)


if __name__ == '__main__':
    unittest.main()
