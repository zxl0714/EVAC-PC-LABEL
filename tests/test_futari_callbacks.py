from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FutariCallbacks(unittest.TestCase):
    def test_storage_vtable_tail_branch_has_its_own_entry(self):
        path = ROOT / 'games/futari/recomp/default-functions.toml'
        entries = tomllib.loads(path.read_text())['functions']
        self.assertEqual(entries['0x82199E18']['end'], 0x82199E1C)

    def test_every_character_declares_the_stored_release_callback(self):
        addresses = {
            'hirow-abnormal': 0x881AC930, 'hirow-normal': 0x881AC928,
            'kiniro-abnormal': 0x881AFB10, 'kiniro-normal': 0x881AFB08,
            'palm-abnormal': 0x88199C60, 'palm-normal': 0x881B2978,
            'reco-abnormal': 0x881B0040, 'reco-normal': 0x881B0038,
        }
        for name, address in addresses.items():
            with self.subTest(module=name):
                data = tomllib.loads((ROOT / 'games/futari/recomp' / (name + '-overrides.toml')).read_text())
                functions = {int(key, 0): value for key, value in data['functions'].items()}
                self.assertEqual(functions[address]['end'], address + 128)
        normal = tomllib.loads((ROOT / 'games/futari/recomp/reco-normal-overrides.toml').read_text())
        functions = {int(key, 0): value for key, value in normal['functions'].items()}
        self.assertEqual(functions[0x881B5C28]['end'], 0x881B5C78)
        for address in (0x8817D190, 0x881E4E80):
            self.assertEqual(functions[address]['end'], address + 8)


    def test_reco_normal_and_abnormal_share_reviewed_callback_bodies(self):
        def functions(variant):
            path = ROOT / 'games/futari/recomp' / f'reco-{variant}-overrides.toml'
            entries = tomllib.loads(path.read_text())['functions']
            return {int(start, 0): entry['end'] - int(start, 0) for start, entry in entries.items()}

        normal, abnormal = functions('normal'), functions('abnormal')
        # These five early entries have identical addresses in the reviewed pair.
        # The six later bodies have moved by eight bytes in Reco Abnormal.
        early = (0x8809ECA0, 0x8809F7A8, 0x880A4DB0, 0x880B1020, 0x880B9C98)
        later = (0x8816AD28, 0x8817D190, 0x881B0038, 0x881B5C28, 0x881C2FD0, 0x881E4E80)
        for normal_address, delta in [(a, 0) for a in early] + [(a, 8) for a in later]:
            with self.subTest(normal=hex(normal_address)):
                self.assertEqual(normal[normal_address], abnormal[normal_address + delta])


if __name__ == '__main__':
    unittest.main()
