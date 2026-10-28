from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/latency'))
from rebuild_xenia_pixels import keyboard_passthrough


class XeniaKeyboardBuildTests(unittest.TestCase):
    def test_only_navigation_guard_and_private_outputs_change(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root/'hid/input_system.cc'
            source.parent.mkdir()
            original = ('before\nif (evac_latency::test_pad && user_index == 0) {\n'
                        '  synthetic();\n}\nreturn original_driver();\n')
            source.write_text(original)
            output = root/'private'
            output.mkdir()
            entries = [dict(file=str(source).replace('\\','/'), output='old.obj',
                command=f'cl /c "{source}" /Foold.obj /Fdold.pdb'),
                dict(file='',output='xenia-hid.lib',
                     command='cmd.exe /C "cd . && lib /out:xenia-hid.lib old.obj other.obj && cd ."')]
            remembered = []
            plan = keyboard_passthrough(entries, output, remembered.append)
            rewritten = Path(plan['copy']).read_text()
            self.assertEqual(source.read_text(), original)
            self.assertEqual(rewritten.replace(' &&\n      evac_latency::CurrentTestCommand() < 900',''), original)
            self.assertIn(str(output/'input_system.cc.obj'), plan['archive'])
            self.assertIn('other.obj', plan['archive'])
            self.assertNotIn('old.obj', plan['archive'])
            self.assertIn('/Fo'+str(output/'input_system.cc.obj'), plan['compile'])
            self.assertIn('/Fd'+str(output/'input.pdb'), plan['compile'])

    def test_missing_or_ambiguous_guard_is_rejected(self):
        for count in (0, 2):
            with self.subTest(count=count), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                source = root/'hid/input_system.cc'
                source.parent.mkdir()
                source.write_text('if (evac_latency::test_pad && user_index == 0) {\n'*count)
                entries = [dict(file=str(source).replace('\\','/'),output='old.obj'),
                           dict(file='',output='xenia-hid.lib')]
                with self.assertRaisesRegex(ValueError, 'guard changed'):
                    keyboard_passthrough(entries, root, lambda arg:None)
                self.assertFalse((root/'input_system.cc').exists())


if __name__ == '__main__':
    unittest.main()
