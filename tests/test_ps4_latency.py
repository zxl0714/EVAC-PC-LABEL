import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from ps4_latency import ps4_buttons, summarize
from test_latency_probe import event, trace


def fixture():
    return [event(1, 100, 14, 101, 0x1008, 1, thread=30),
            event(2, 200, 18, 1, 1, 1000),
            event(3, 210, 20, 2, 0, 0x4020, 800, 1, 1),
            event(4, 220, 19, 2, 1, 1),
            event(5, 300, 26, 0x8000, 1280, 720, thread=40),
            event(6, 400, 21, 5, 0x6000, 0x8000, 0x9000, 10, 1, thread=40),
            event(7, 600, 22, 6, 0x4000, 2, 2, 1, 0, thread=50),
            event(8, 610, 23, 7, 6, 0, thread=50),
            event(9, 650, 14, 101, 0, 0, thread=30)]


class Ps4LatencyTests(unittest.TestCase):
    def test_independent_chains_never_imply_input_to_image(self):
        report = summarize(trace(fixture()))
        self.assertEqual(report['paired_input_sources'], 1)
        self.assertEqual(report['owned_output_images'], 1)
        self.assertIn('input_to_first_responsive_image', report['unmeasured'])
        self.assertNotIn('source_to_present_us', report['statistics'])

    def test_bad_source_mask_rejected(self):
        rows = fixture()
        rows[2]['c'] = 0x4080
        with self.assertRaises(ValueError): summarize(trace(rows))

    def test_wrong_frame_not_paired_to_next_present(self):
        rows = fixture()
        rows[5]['c'] = 0x8100
        with self.assertRaises(ValueError): summarize(trace(rows))

    def test_failed_and_reused_present_not_counted(self):
        for field, value in (('result', 0xffffffff), ('reuse', 1), ('game', 0)):
            rows = fixture()
            if field == 'result': rows[7]['c'] = value
            elif field == 'reuse': rows[6]['f'] = value
            else: rows[6]['e'] = value
            self.assertEqual(summarize(trace(rows))['owned_output_images'], 0)

    def test_missing_duplicate_or_out_of_order_rejected(self):
        for mutate in (lambda r: r[2].update(b=1), lambda r: r[7].update(a=6),
                       lambda r: r[6].update(qpc=350), lambda r: r[3].update(c=0)):
            rows = fixture()
            mutate(rows)
            with self.assertRaises(ValueError): summarize(trace(rows))

    def test_suboptimal_is_presented(self):
        rows = fixture()
        rows[7]['c'] = 1000001003
        self.assertEqual(summarize(trace(rows))['owned_output_images'], 1)

    def test_no_source_or_unfinished_call_is_not_filled(self):
        rows = fixture()[:3]
        report = summarize(trace(rows))
        self.assertEqual(report['unpaired_commands'], [101])
        self.assertEqual(report['unfinished_pad_calls'], 1)
        self.assertEqual(report['paired_input_sources'], 0)

    def test_controller_mapping(self):
        self.assertEqual(ps4_buttons(0xf), 0xf0)
        self.assertEqual(ps4_buttons(0xf000), 0xf000)
        self.assertEqual(ps4_buttons(0x20), 0x100000)

    def test_read_state_returns_zero_but_one_report(self):
        rows = fixture()
        rows[1]['e'] = 1
        rows[3]['b'] = 0
        self.assertEqual(summarize(trace(rows))['paired_input_sources'], 1)

    def test_navigation_and_incomplete_window_not_in_distribution(self):
        rows = fixture()
        report = summarize(trace(rows))
        self.assertEqual(report['measured_output_images'], 1)
        rows[4]['qpc'] = 50
        self.assertEqual(summarize(trace(rows))['measured_output_images'], 0)
        report = summarize(trace(fixture()[:-1]))
        self.assertIsNone(report['measurement_window'])
        self.assertEqual(report['measured_output_images'], 0)

    def test_shad_original_controller_queue_is_exactly_paired(self):
        rows = fixture()
        rows[1]['f'] = 0x777
        rows[2]['f'] = 0
        rows.append(event(10, 150, 27, 0x777, 1, 800, 0x4020, thread=60))
        report = summarize(trace(rows))
        self.assertEqual(report['paired_input_sources'], 1)
        self.assertEqual(report['input_samples'][0]['source'], 1)
        # Same buttons on another controller or a different timestamp do not link.
        rows[-1]['a'] = 0x778
        self.assertEqual(summarize(trace(rows))['paired_input_sources'], 0)
        rows[-1]['a'] = 0x777
        rows[-1]['c'] = 799
        self.assertEqual(summarize(trace(rows))['paired_input_sources'], 0)

    def test_shad_ambiguous_sample_identity_is_rejected(self):
        rows = fixture()
        rows += [event(10, 150, 27, 0x777, 1, 800, 0x4020, thread=60),
                 event(11, 140, 14, 102, 0x1008, 1, thread=30),
                 event(12, 160, 27, 0x777, 11, 800, 0x4020, thread=60)]
        with self.assertRaises(ValueError): summarize(trace(rows))


if __name__ == '__main__': unittest.main()
