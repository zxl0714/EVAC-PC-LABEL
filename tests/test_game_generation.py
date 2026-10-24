import copy
import hashlib
from pathlib import Path
import sys
import tempfile
import tomllib
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import generate_catalogue
import generate_game
from project import ROOT


class GameGenerationTests(unittest.TestCase):
    def setUp(self):
        self.game = generate_catalogue.load(ROOT / 'games/catalogue.json')[1]
        self.source = ROOT / 'games/espgaluda2/recomp'
        self.manifest = tomllib.loads((self.source / 'espgaluda2.toml').read_text(encoding='utf-8'))

    def test_all_eight_programs_and_separate_overlays(self):
        records = generate_game.targets(self.manifest, self.game)
        self.assertEqual(list(records), ['default', 'dat00', 'dat11', 'dat12', 'dat15', 'dat162', 'dat163', 'dat164'])

    def test_missing_duplicate_or_redirected_target_rejected(self):
        missing = copy.deepcopy(self.manifest)
        missing['modules'].pop()
        duplicate = copy.deepcopy(self.manifest)
        duplicate['modules'].append(duplicate['modules'][0])
        redirected = copy.deepcopy(self.manifest)
        redirected['entrypoint']['out_directory_path'] = '../../../game-data/espgaluda2/default'
        for manifest in (missing, duplicate, redirected):
            with self.assertRaises(ValueError):
                generate_game.targets(manifest, self.game)

    def test_execution_manifest_supports_external_unicode_path(self):
        with tempfile.TemporaryDirectory(prefix='合集 空格 ') as directory:
            root = Path(directory).resolve()
            records = generate_game.targets(self.manifest, self.game)
            result = tomllib.loads(generate_game.execution_manifest(
                self.game, records, root / '外部数据', root / 'generated', self.source))
            self.assertEqual(Path(result['project']['game_root']), root / '外部数据')
            self.assertEqual(Path(result['entrypoint']['out_directory_path']), root / 'generated/default')
            self.assertTrue(all(Path(path).is_absolute() for path in result['entrypoint']['includes']))
            self.assertEqual(len(result['modules']), 7)

    def test_registry_uses_packaged_game_module_directory(self):
        source = generate_game.registry_source(generate_game.targets(self.manifest, self.game), self.game['id'])
        self.assertEqual(source.count('RegisterRecompiledModule('), 7)
        self.assertIn('parent_path()/"games/espgaluda2"', source)
        self.assertNotIn('PPC_FUNC_IMPL', source)

    def test_novice_indirect_callback_is_declared_only_for_its_overlay(self):
        records = generate_game.targets(self.manifest, self.game)
        self.assertEqual(records['dat163']['includes'], ['dat163-functions.toml'])
        declarations = tomllib.loads((self.source / 'dat163-functions.toml').read_text(encoding='utf-8'))
        self.assertEqual(declarations['functions']['0x880EEB00'], {'size': 12})
        for name, record in records.items():
            if name != 'dat163':
                self.assertNotIn('dat163-functions.toml', record.get('includes', []))
        result = tomllib.loads(generate_game.execution_manifest(
            self.game, records, ROOT / 'game-data/espgaluda2', ROOT / 'generated/espgaluda2', self.source))
        novice = next(item for item in result['modules'] if item['guest_path'] == 'dat163.xex')
        self.assertEqual(novice['includes'], [str(self.source / 'dat163-functions.toml').replace('\\', '/')])

    def test_synthetic_data_version_check_is_read_only(self):
        with tempfile.TemporaryDirectory() as directory:
            data = Path(directory)
            path = data / 'authored.bin'
            path.write_bytes(b'authored fixture')
            item = dict(path=path.name, bytes=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            expected = dict(files=[item])
            self.assertEqual(generate_game.verify_data(expected, data), [item])
            self.assertEqual(list(data.iterdir()), [path])
            path.write_bytes(b'changed fixture')
            with self.assertRaisesRegex(ValueError, 'Unsupported data'):
                generate_game.verify_data(expected, data)

    def test_generator_success_with_unresolved_branch_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module = root / 'default'
            module.mkdir()
            source = module / 'fixture.cpp'
            source.write_text('void fixture() {}\n', encoding='utf-8')
            generate_game.verify_generated_branches(root, {'default': {}})
            for failure in ('REX_FATAL("Unresolved branch");', '// ERROR: conditional branch'):
                source.write_text(failure, encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'default/fixture.cpp:1'):
                    generate_game.verify_generated_branches(root, {'default': {}})
                self.assertEqual(source.read_text(encoding='utf-8'), failure)


if __name__ == '__main__':
    unittest.main()
