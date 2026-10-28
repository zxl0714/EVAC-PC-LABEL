"""Reject incomplete or unbounded GPU pixel observer settings before startup."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/latency'))
from launch_xenia_probe import region_text


class XeniaLaunchTests(unittest.TestCase):
    def test_all_five_region_fields_are_preserved(self):
        self.assertEqual(region_text([640,720,640,360,400]),'640,720,640,360,400')

    def test_missing_capacity_is_not_silently_disabled(self):
        with self.assertRaises(ValueError):
            region_text([640,720,640,360])

    def test_invalid_or_excessive_allocation_is_rejected(self):
        for values in ([0,0,1024,1024,1024],[-1,0,100,100,10],[0,0,0,100,10],[0,0,100,100,0]):
            with self.subTest(values=values), self.assertRaises(ValueError):
                region_text(values)


if __name__=='__main__':
    unittest.main()
