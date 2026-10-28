"""Keep first-response subsets, paired totals, theory and photons distinct."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/latency'))
from finalize_input_latency import manual_result,pixel_result,screen_budget


class FinalLatencyReportTests(unittest.TestCase):
    def test_144hz_center_models(self):
        self.assertAlmostEqual(screen_budget(144,'immediate')['mean_ms'],1000/144/2)
        self.assertAlmostEqual(screen_budget(144,'empty_fifo')['mean_ms'],1000/144)
        self.assertIsNone(screen_budget(144,'empty_fifo')['panel_delay_ms'])

    def test_fifo_scan_position_changes_budget(self):
        self.assertAlmostEqual(screen_budget(144,'empty_fifo',0)['mean_ms'],1000/144/2)
        self.assertAlmostEqual(screen_budget(144,'empty_fifo',1)['mean_ms'],1000/144*1.5)

    def test_invalid_theory_inputs(self):
        for hz,mode,position in ((0,'immediate',.5),(144,'unknown',.5),(144,'empty_fifo',2)):
            with self.assertRaises(ValueError):screen_budget(hz,mode,position)

    def test_direct_total_not_sum_of_segment_medians(self):
        record={'accepted':[{'source_to_response_present_us':a+b,'core_to_response_present_us':b}
                            for a,b in ((1000,9000),(5000,5000),(9000,9000))]}
        result=pixel_result(record)
        self.assertEqual(result['input_available_to_response_present']['median_ms'],10)
        self.assertEqual(result['input_available_to_core']['median_ms']+result['core_to_response_present']['median_ms'],14)

    def test_single_response_does_not_get_population_tail(self):
        result=manual_result({'accepted_responses':1,'core':20,'total':25},'core','total')
        self.assertEqual(result['input_available_to_core']['median_ms'],5)
        self.assertIsNone(result['core_to_response_present']['p95_ms'])

    def test_reversed_response_clock_rejected(self):
        with self.assertRaises(ValueError):pixel_result({'accepted':[{'source_to_response_present_us':1,'core_to_response_present_us':2}]})


if __name__=='__main__':unittest.main()
