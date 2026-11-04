from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FutariCallbacks(unittest.TestCase):
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


if __name__ == '__main__':
    unittest.main()
