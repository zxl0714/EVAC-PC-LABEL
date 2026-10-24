from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import g2_callback_fixture


class CallbackFixtureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.patch = patch.object(g2_callback_fixture, 'ROOT', self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        self.folder = self.root / 'generated/espgaluda2/dat163'
        self.folder.mkdir(parents=True)
        self.registry = self.folder / 'espgaluda2_register.cpp'
        self.registrations = ''.join(f'SetFunction(0x{a}, sub_{a});'
                                    for a in ('880EEB00', '880EE9E8', '880EEB10'))
        self.registry.write_text(self.registrations, encoding='utf-8')
        self.body = 'DEFINE_REX_FUNC(sub_880EEB00) {\n    AuthoredSyntheticBody();\n}'
        (self.folder / 'espgaluda2_recomp.0.cpp').write_text(self.body, encoding='utf-8')
        self.output = self.root / '.local/callback.inc'

    def test_derived_fixture_stays_local_and_records_identities(self):
        result = g2_callback_fixture.prepare(self.output)
        self.assertTrue(self.output.read_text(encoding='utf-8').endswith(self.body + '\n'))
        self.assertIn('#define G2_CALLBACK_TARGET sub_880EE9E8', self.output.read_text())
        self.assertFalse(result['guestExecution'])
        self.assertFalse(result['gameLoop'])
        self.assertEqual(result['callback'], '0x880EEB00')
        for name in ('generatedSha256', 'registrySha256', 'fixtureSha256'):
            self.assertEqual(len(result[name]), 64)

    def test_rom_and_source_destinations_are_rejected_before_writing(self):
        for name in ('game-data/callback.inc', 'games/callback.inc', 'fixture.inc'):
            output = self.root / name
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'remain local'):
                g2_callback_fixture.prepare(output)
            self.assertFalse(output.exists())

    def test_missing_duplicate_registration_or_body_fails(self):
        for registry in ('', 'SetFunction(0x880EEB00, sub_880EEB00);' * 2):
            self.registry.write_text(registry, encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'registered exactly once'):
                g2_callback_fixture.prepare(self.output)
        self.registry.write_text(self.registrations, encoding='utf-8')
        (self.folder / 'espgaluda2_recomp.1.cpp').write_text(self.body, encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'precisely one'):
            g2_callback_fixture.prepare(self.output)
        for path in self.folder.glob('*recomp*.cpp'):
            path.write_text('// authored empty fixture', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'precisely one'):
            g2_callback_fixture.prepare(self.output)

    def test_all_overlay_declarations_and_module_isolation(self):
        import tomllib
        real_root = Path(__file__).resolve().parents[1]
        source = real_root / 'games/espgaluda2/recomp'
        manifest = tomllib.loads((source / 'espgaluda2.toml').read_text())
        for module, (entry, target) in g2_callback_fixture.CALLBACKS.items():
            with self.subTest(module=module):
                declaration = module + '-functions.toml'
                record = next(x for x in manifest['modules'] if x['guest_path'] == module + '.xex')
                self.assertIn(declaration, record['includes'])
                functions = tomllib.loads((source / declaration).read_text())['functions']
                self.assertEqual(functions['0x' + entry], {'size': 12})
                folder = self.root / 'generated/espgaluda2' / module
                folder.mkdir(parents=True, exist_ok=True)
                registrations = ''.join(f'SetFunction(0x{a}, sub_{a});'
                    for a in (entry, target, f'{int(entry, 16) + 16:08X}'))
                (folder / 'espgaluda2_register.cpp').write_text(registrations)
                body = f'DEFINE_REX_FUNC(sub_{entry}) {{\n    AuthoredSyntheticBody();\n}}'
                (folder / 'espgaluda2_recomp.0.cpp').write_text(body)
                result = g2_callback_fixture.prepare(self.output, module)
                self.assertEqual(result['module'], module)
                self.assertEqual(result['callback'], '0x' + entry)
                self.assertEqual(result['target'], '0x' + target)

    def test_unknown_module_rejected(self):
        with self.assertRaisesRegex(ValueError, 'declared G2 overlay'):
            g2_callback_fixture.prepare(self.output, '../dat12')


if __name__ == '__main__':
    unittest.main()
