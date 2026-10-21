from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import g2_callback_fixture


class NoviceFixtureTests(unittest.TestCase):
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
        self.registry.write_text('SetFunction(0x880EEB00, sub_880EEB00);', encoding='utf-8')
        self.body = 'DEFINE_REX_FUNC(sub_880EEB00) {\n    AuthoredSyntheticBody();\n}'
        (self.folder / 'espgaluda2_recomp.0.cpp').write_text(self.body, encoding='utf-8')
        self.output = self.root / '.local/callback.inc'

    def test_derived_fixture_stays_local_and_records_identities(self):
        result = g2_callback_fixture.prepare(self.output)
        self.assertEqual(self.output.read_text(encoding='utf-8'), self.body + '\n')
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
        self.registry.write_text('SetFunction(0x880EEB00, sub_880EEB00);', encoding='utf-8')
        (self.folder / 'espgaluda2_recomp.1.cpp').write_text(self.body, encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'precisely one'):
            g2_callback_fixture.prepare(self.output)
        for path in self.folder.glob('*recomp*.cpp'):
            path.write_text('// authored empty fixture', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'precisely one'):
            g2_callback_fixture.prepare(self.output)


if __name__ == '__main__':
    unittest.main()
