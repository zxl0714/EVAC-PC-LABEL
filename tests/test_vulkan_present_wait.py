"""Reject invalid presentation bounds instead of clamping or inventing samples."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from vulkan_present_wait import analyze


def capture():
    return [dict(kind='header', pid=42, frequency=1000, completion_upper_bound=True),
            dict(kind='wait', id=1, source=900, begin=10, end=11, observed=12, result=0),
            dict(kind='wait', id=2, source=950, begin=30, end=31, observed=33, result=0),
            dict(kind='end', requests=2, completed=2, dropped=0)]


class PresentWaitTests(unittest.TestCase):
    def test_exact_clock_and_warmup(self):
        result = analyze(capture(), 1)
        self.assertEqual(result['present_to_completion_bound_ms']['p50_ms'], 3)

    def test_timeout_or_dropped_present_is_not_a_timing_sample(self):
        for index, key, value in ((1, 'result', 2), (-1, 'dropped', 1),
                                  (-1, 'requests', 3), (2, 'id', 3)):
            rows = capture(); rows[index][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                analyze(rows, 0)

    def test_clock_reversal_or_source_reuse_is_rejected(self):
        for index, key, value in ((1, 'observed', 9), (1, 'end', 13),
                                  (2, 'source', 900), (1, 'begin', 0)):
            rows = capture(); rows[index][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                analyze(rows, 0)

    def test_truncated_or_empty_steady_capture_is_rejected(self):
        for rows, warmup in ((capture()[:-1], 0), (capture(), 2), (capture(), -1)):
            with self.subTest(warmup=warmup), self.assertRaises(ValueError):
                analyze(copy.deepcopy(rows), warmup)

    def test_imprecise_endpoint_must_be_explicit(self):
        rows = capture(); del rows[0]['completion_upper_bound']
        with self.assertRaises(ValueError):
            analyze(rows, 0)


if __name__ == '__main__':
    unittest.main()
