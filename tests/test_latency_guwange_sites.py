"""Keep high-frequency original busy-wait back-edges out of timing probes."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/latency'))
from prepare_guwange_xenia import SITES


class GuwangeSiteTests(unittest.TestCase):
    def test_observe_once_before_wait_not_each_sleep_zero_iteration(self):
        sites = {identifier: pc for identifier, pc, _ in SITES['default']}
        self.assertEqual(sites[16], 0x821F74A8)
        self.assertEqual(sites[17], 0x821F74C0)
        self.assertFalse(set(sites.values()) & set(range(0x821F74AC, 0x821F74C0, 4)))

    def test_sites_have_unique_ids_addresses_and_four_original_registers(self):
        identifiers = []
        for specs in SITES.values():
            self.assertEqual(len({pc for _, pc, _ in specs}), len(specs))
            for identifier, _, registers in specs:
                identifiers.append(identifier)
                self.assertEqual(len(registers), 4)
                self.assertTrue(all(0 <= reg <= 33 for reg in registers))
        self.assertEqual(len(set(identifiers)), len(identifiers))


if __name__ == '__main__':
    unittest.main()
