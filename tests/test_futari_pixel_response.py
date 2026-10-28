import sys
from pathlib import Path
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from futari_pixel_response import locate


class SpriteLocationTests(unittest.TestCase):
    def test_known_motion_with_independent_scrolling_background(self):
        rng = np.random.default_rng(1729)
        sprite = rng.integers(0, 256, (24, 18, 3), dtype=np.uint8)
        for shift in (-12, -7, -1, 0, 1, 6, 12):
            image = rng.integers(0, 256, (50, 120, 3), dtype=np.uint8)
            image[10:34, 50+shift:68+shift] = sprite
            point = locate(image, sprite, 10, 10, 90)
            self.assertEqual(point['x'], 50+shift)
            self.assertAlmostEqual(point['score'], 1.0)
            self.assertGreater(point['score']-point['distant_score'], .8)

    def test_absent_sprite_is_not_a_confident_location(self):
        rng = np.random.default_rng(3)
        sprite = rng.integers(0, 256, (24, 18, 3), dtype=np.uint8)
        image = rng.integers(0, 256, (50, 120, 3), dtype=np.uint8)
        self.assertLess(locate(image, sprite, 10, 10, 90)['score'], .8)

    def test_blank_template_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'no image features'):
            locate(np.zeros((50,120,3)), np.zeros((24,18,3)), 10, 10, 90)


if __name__ == '__main__': unittest.main()
