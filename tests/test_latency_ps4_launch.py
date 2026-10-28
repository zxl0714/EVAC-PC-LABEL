"""Reject ROI settings the private GPU observer cannot record before launch."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/latency'))
from launch_ps4_probe import checked_pixel_region
from launch_shad_probe import checked_pixel_region as shad_region


class Ps4LaunchTests(unittest.TestCase):
    def test_both_launchers_accept_supported_crop(self):
        for check in (checked_pixel_region, shad_region):
            self.assertEqual(check('320,0,640,720,30'), '320,0,640,720,30')

    def test_full_1280_image_is_rejected_before_silent_capture_failure(self):
        for check in (checked_pixel_region, shad_region):
            with self.assertRaises(ValueError):
                check('0,0,1280,720,30')

    def test_invalid_or_excessive_capture_is_rejected(self):
        for text in ('0,0,640,720', '-1,0,640,720,30', '0,0,0,720,30',
                     '0,0,640,720,0', '0,0,1024,1024,1024',
                     '4294967296,0,640,720,30'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                checked_pixel_region(text)


if __name__ == '__main__':
    unittest.main()
