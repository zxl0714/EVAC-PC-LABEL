"""Private G2 measurement compilation must not overwrite production outputs."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/latency'))
import prepare_g2_probe
from rebuild_g2_probe import compile_command


class PrivateG2BuildTests(unittest.TestCase):
    def test_unique_original_sites(self):
        sites = [(m, i, pc, regs) for m, specs in prepare_g2_probe.SITES.items() for i, pc, regs in specs]
        self.assertEqual(len(sites), 18)
        self.assertEqual(len({i for _, i, _, _ in sites}), len(sites))
        self.assertEqual(len({(m, pc) for m, _, pc, _ in sites}), len(sites))
        self.assertTrue(all(pc % 4 == 0 and len(regs) == 4 and all(0 <= r <= 33 for r in regs)
                            for _, _, pc, regs in sites))

    def test_observation_avoids_production_wait_hook(self):
        sites = prepare_g2_probe.SITES['default']
        addresses = {pc for _, pc, _ in sites}
        self.assertNotIn(0x821713A4, addresses)
        self.assertIn(0x821713A0, addresses)
        self.assertIn(0x821713A8, addresses)
        self.assertEqual(next(pc for i,pc,_ in sites if i==6), 0x82170F84)

    def test_redirect_all_compilation_outputs(self):
        entry = dict(file='E:/original/source.cpp', command='clang++ -MT original.obj -MF original.obj.d -o original.obj -c E:/original/source.cpp')
        result = compile_command(entry, Path('E:/private/copy.cpp'), Path('E:/private/new.obj'),
                                 [Path('E:/private/include')], True)
        joined = ' '.join(result).replace('\\', '/')
        self.assertNotIn('original.obj', joined)
        self.assertNotIn('original/source.cpp', joined)
        self.assertIn('-MF E:/private/new.obj.d', joined)
        self.assertIn('-DEVAC_LATENCY_HOST=1', result)

    def test_refuse_recipe_without_output(self):
        with self.assertRaisesRegex(ValueError, 'redirection'):
            compile_command(dict(file='original.cpp', command='clang++ -c original.cpp'),
                            Path('copy.cpp'), Path('copy.obj'), [])


if __name__ == '__main__':
    unittest.main()
