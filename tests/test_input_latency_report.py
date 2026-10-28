from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from input_latency_report import adapt_sdoj, distribution, summarize


def sample(identity='edge-1', **changes):
    return dict(id=identity, selection='gameplay', lineage_verified=True,
                points_us=dict(window_event=100, game_button_copy=200,
                               logic_copy=250, guest_submit=1000,
                               present_begin=1100, present_return=1200)) | changes


class InputLatencyReportTests(unittest.TestCase):
    def test_pairwise_total_and_exclusive_segments(self):
        result = summarize([sample()], 60)
        row = result['responses'][0]
        self.assertEqual(row['intervals_us']['event_to_present_return'], 1100)
        self.assertEqual(row['intervals_us']['logic_copy_to_guest_submit'], 750)
        self.assertEqual(result['statistics']['event_to_present_return']['median_ms'], 1.1)
        self.assertIn('present_to_first_display', result['unmeasured'])
        self.assertNotIn('physical_total', result['statistics'])

    def test_small_probe_has_no_p95_claim(self):
        stats = distribution([1000, 2000, 3000], 60)
        self.assertIsNone(stats['p95_ms'])
        self.assertEqual(stats['median_game_frames'], .12)
        self.assertEqual(distribution(list(range(1, 21)), 60)['p95_ms'], .019)

    def test_medians_are_not_summed_as_total(self):
        samples = []
        for i, spans in enumerate(((1, 1, 9, 0, 0), (1, 9, 1, 0, 0), (9, 1, 1, 0, 0))):
            points = {'window_event': 0}
            timestamp = 0
            for name, delta in zip(('game_button_copy', 'logic_copy', 'guest_submit',
                                    'present_begin', 'present_return'), spans):
                timestamp += delta
                points[name] = timestamp
            samples.append(sample(str(i), points_us=points))
        result = summarize(samples, 60)
        self.assertEqual(result['statistics']['event_to_present_return']['median_ms'], .011)
        self.assertEqual(result['statistics']['logic_copy_to_guest_submit']['median_ms'], .001)

    def test_menu_and_unpaired_do_not_become_zero_latency(self):
        result = summarize([sample('menu', selection='continue_screen'),
                            sample('lost', lineage_verified=False)], 60)
        self.assertEqual(result['accepted_responses'], 0)
        self.assertEqual(result['rejected_responses'],
                         {'continue_screen': 1, 'missing_input_output_lineage': 1})
        self.assertIsNone(result['statistics']['event_to_present_return']['median_ms'])

    def test_nonmonotonic_missing_nonfinite_and_boolean_times_rejected(self):
        for value in (-1, 99, None, float('nan'), True):
            row = sample()
            row['points_us']['logic_copy'] = value
            with self.subTest(value=value):
                self.assertEqual(summarize([row], 60)['accepted_responses'], 0)
        row = sample()
        del row['points_us']['present_return']
        self.assertEqual(summarize([row], 60)['accepted_responses'], 0)

    def test_duplicate_edges_and_invalid_rate_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            summarize([sample(), sample()], 60)
        for rate in (0, -1, float('nan'), '60'):
            with self.assertRaises(ValueError):
                summarize([sample()], rate)

    def test_sdoj_adapter_requires_same_copy_first_result_and_success(self):
        report = {'input_edges': [dict(edge=dict(press=dict(t_us=100, key=74),
                                                 raw=dict(t_us=200), copied=dict(t_us=250, seq=9)),
                                       output=dict(lineage=dict(copy=dict(seq=9)),
                                                   first_result_output=True, submit=dict(t_us=1000),
                                                   present=dict(begin_us=1100, t_us=1200, frame=1,
                                                                hr=0, guest_drawn=True)))]}
        selection = dict(input_origin='computer_use_keyboard',
                         responses=[dict(event_us=100, classification='gameplay', evidence='observed stage1')])
        rows = adapt_sdoj(report, selection)
        self.assertEqual(summarize(rows, 60)['accepted_responses'], 1)
        for mutate in ('wrong_copy', 'held_picture', 'failed_present', 'ui_only'):
            bad = deepcopy(report)
            output = bad['input_edges'][0]['output']
            if mutate == 'wrong_copy': output['lineage']['copy']['seq'] = 10
            if mutate == 'held_picture': output['first_result_output'] = False
            if mutate == 'failed_present': output['present']['hr'] = 1
            if mutate == 'ui_only': output['present']['guest_drawn'] = False
            self.assertEqual(summarize(adapt_sdoj(bad, selection), 60)['accepted_responses'], 0)
        selection['responses'][0]['event_us'] = 101
        with self.assertRaisesRegex(ValueError, 'absent'):
            adapt_sdoj(report, selection)


if __name__ == '__main__':
    unittest.main()
