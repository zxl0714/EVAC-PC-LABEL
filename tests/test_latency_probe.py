import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from latency_probe import COLUMNS, guest_submits, keyboard_coverage, read_trace, summarize_trace


def event(seq, qpc, kind, *args, thread=10):
    return dict(zip(COLUMNS, (seq, qpc, thread, kind, *args, *([0] * (6 - len(args))))))


def trace(rows):
    return dict(records=rows, frequency=1000000, pid=1, committed=len(rows),
                complete=True, incomplete=0, overflow=0)


def output_rows():
    return [event(1, 100, 6, 1234, 5678, 1280, 720),
            event(2, 200, 7, 1, 0, 1280, 720, 1),
            event(3, 400, 8, 2, 99, 0, 1, 1),
            event(4, 450, 9, 3, 2, 0)]


class LatencyProbeTests(unittest.TestCase):
    def test_keyboard_coverage_keeps_misses_and_bindings_separate(self):
        edges = [dict(binding=1, status='observed_by_guest_api', duration_ms=.5,
                      event_to_api_return_ms=.2),
                 dict(binding=1, status='not_observed_by_guest_api', duration_ms=.7),
                 dict(binding=2, status='capture_censored', duration_ms=None)]
        summary = keyboard_coverage(edges)
        self.assertEqual(summary['bound_presses'], 3)
        self.assertEqual(summary['statuses']['capture_censored'], 1)
        self.assertEqual(len(summary['bindings']), 2)
        self.assertEqual(summary['bindings'][0]['observed_event_to_api_ms'], [.2])
        self.assertEqual(summary['bindings'][0]['held_duration']['count'], 2)
        self.assertFalse(summary['bindings'][1]['observed_event_to_api_ms'])
        self.assertIn('overlap-conditioned',summary['warning'])

    def test_zero_dxgi_stats_are_not_display_evidence(self):
        rows = output_rows() + [event(5, 460, 16, 3, 1, 0, 0, 0, 0),
                                event(6, 461, 17, 5, 0, 0, 0)]
        result = summarize_trace(trace(rows))
        self.assertEqual(result['dxgi_feedback']['queries'], 1)
        self.assertEqual(result['dxgi_feedback']['populated'], 0)
        self.assertIn('first_display', result['unmeasured'])

    def test_populated_dxgi_stats_preserve_actual_identity(self):
        rows = output_rows() + [event(5, 460, 16, 3, 2, 0, 1, 420, 0),
                                event(6, 461, 17, 5, 75, 76, 0)]
        result = summarize_trace(trace(rows))
        self.assertEqual(result['dxgi_feedback']['populated'], 1)
        self.assertEqual(result['dxgi_feedback']['observations'][0]['present_count'], 1)
        self.assertIn('first_display', result['unmeasured'])

    def test_dxgi_bad_link_is_not_accepted(self):
        rows = output_rows() + [event(5, 460, 16, 99, 2, 0, 1, 420, 0),
                                event(6, 461, 17, 5, 75, 76, 0)]
        self.assertEqual(summarize_trace(trace(rows))['dxgi_feedback']['invalid_links'], [6])

    def test_synthetic_input_is_explicit_and_paired(self):
        rows = [event(1, 100, 14, 77, 4, 1, thread=20), event(2, 110, 3, 0),
                event(3, 115, 15, 1, 0, 1, 4), event(4, 120, 4, 2, 0, 0, 1, 4)]
        result = summarize_trace(trace(rows))
        self.assertFalse(result['keyboard_responses'])
        item = result['synthetic_guest_responses'][0]
        self.assertEqual(item['input_source'], 'synthetic_guest_controller')
        self.assertEqual(item['source_to_api_return_ms'], .02)
        self.assertIn('input_to_present', result['unmeasured'])
        rows[3]['e'] = 8
        self.assertEqual(summarize_trace(trace(rows))['synthetic_guest_responses'][0]['status'],
                         'not_observed_in_capture')

    def test_synthetic_invalid_identity_and_buttons_rejected(self):
        for row in (event(2, 110, 15, 99, 0, 1, 4), event(2, 110, 15, 1, 0, 1, 8)):
            with self.assertRaises(ValueError):
                summarize_trace(trace([event(1, 100, 14, 77, 4, 1), row]))

    def test_owned_output_not_arbitrary_next_frame(self):
        result = summarize_trace(trace(output_rows()))
        self.assertEqual(result['statistics']['swap_to_present_us']['median_ms'], .3)
        self.assertEqual(result['output_responses'][0]['swap_seq'], 1)
        self.assertIn('input_to_present', result['unmeasured'])

    def test_repeated_present_excluded_from_first_response_stats(self):
        rows = output_rows() + [event(5, 500, 8, 2, 99, 1, 1, 1), event(6, 550, 9, 5, 2, 0)]
        result = summarize_trace(trace(rows))
        self.assertEqual(len(result['output_responses']), 2)
        self.assertEqual(result['statistics']['swap_to_present_us']['count'], 1)

    def test_failed_and_ui_only_present_do_not_consume_ready(self):
        for field, value in [('result', 1), ('draw', 0)]:
            rows = output_rows()
            rows[3]['c'] = value if field == 'result' else 0
            rows[2]['d'] = value if field == 'draw' else 1
            result = summarize_trace(trace(rows))
            self.assertFalse(result['output_responses'])
            self.assertEqual(result['ready_without_successful_present'], [2])

    def test_reused_mailbox_does_not_replace_identity(self):
        rows = output_rows()
        rows.insert(2, event(5, 300, 7, 1, 0, 1280, 720, 1))
        result = summarize_trace(trace(rows))
        self.assertEqual(result['output_responses'][0]['ready_seq'], 2)
        self.assertEqual(result['ready_without_successful_present'], [5])

    def test_wrong_identity_thread_and_size_rejected(self):
        for index, field, value in [(3, 'b', 5), (3, 'thread', 99), (1, 'c', 640)]:
            rows = output_rows()
            rows[index][field] = value
            result = summarize_trace(trace(rows))
            self.assertFalse(result['output_responses'])
            self.assertEqual(len(result['invalid_links']), 1)

    def test_input_api_span_is_not_game_logic_delay(self):
        rows = [event(1, 100, 3, 0, 0, 1234), event(2, 110, 4, 1, 0, 0, 25, 4)]
        result = summarize_trace(trace(rows))
        self.assertEqual(result['input_calls'][0]['duration_us'], 10)
        self.assertFalse(result['output_responses'])

    def test_capture_accounting_and_missing_footer(self):
        text = ('# evac_latency_v1 frequency=10000000 pid=1 capacity=2\n' +
                ','.join(COLUMNS) + '\n1,10,1,1,0,0,0,0,0,0\n' +
                '# end reserved=1 committed=1 incomplete=0 overflow=0\n')
        self.assertTrue(read_trace(text)['complete'])
        for bad in (text.replace('committed=1', 'committed=2'),
                    text.replace('frequency=10000000', 'frequency=0'),
                    '\n'.join(text.splitlines()[:-1])):
            with self.assertRaises(ValueError): read_trace(bad)
        overflow = text.replace('reserved=1', 'reserved=3').replace('incomplete=0', 'incomplete=1').replace('overflow=0', 'overflow=1')
        self.assertFalse(read_trace(overflow)['complete'])

    def test_clock_reversal_rejected(self):
        rows = output_rows()
        rows[1]['qpc'] = 99
        with self.assertRaises(ValueError): summarize_trace(trace(rows))

    def test_short_press_miss_and_retained_press_identity(self):
        rows = [event(1, 100, 2, 74, 1), event(2, 105, 11, 1, 88, 1, 1, 0),
                event(3, 110, 2, 74, 0), event(4, 120, 3, 0, 0, 1234),
                event(5, 130, 4, 4, 0, 0, 99, 0)]
        result = summarize_trace(trace(rows))
        edge = result['keyboard_responses'][0]
        self.assertEqual(edge['status'], 'not_observed_by_guest_api')
        self.assertEqual(edge['duration_ms'], .01)
        self.assertNotIn('event_to_api_return_ms', edge)
        # Sampling identity, not the next matching button mask, proves reception.
        rows.insert(4, event(6, 125, 12, 1, 88, 99, 0))
        edge = summarize_trace(trace(rows))['keyboard_responses'][0]
        self.assertEqual(edge['status'], 'observed_by_guest_api')
        self.assertEqual(edge['event_to_api_return_ms'], .03)

    def test_wrong_packet_or_user_does_not_pair_keyboard(self):
        rows = [event(1, 100, 2, 74, 1), event(2, 105, 11, 1, 88, 1, 1, 0),
                event(3, 110, 2, 74, 0), event(4, 120, 3, 0, 0, 1234),
                event(5, 125, 12, 1, 88, 98, 0), event(6, 130, 4, 4, 0, 0, 99, 4)]
        self.assertEqual(summarize_trace(trace(rows))['keyboard_responses'][0]['status'],
                         'not_observed_by_guest_api')
        rows[4].update(c=99, d=1)
        self.assertEqual(summarize_trace(trace(rows))['keyboard_responses'][0]['status'],
                         'not_observed_by_guest_api')

    def test_incomplete_capture_is_censored_not_missed(self):
        rows = [event(1, 100, 2, 74, 1), event(2, 105, 11, 1, 88, 1, 1, 0)]
        edge = summarize_trace(trace(rows))['keyboard_responses'][0]
        self.assertEqual(edge['status'], 'capture_censored')

    def test_duplicate_api_end_rejected(self):
        rows = [event(1, 100, 3, 0), event(2, 110, 4, 1, 0, 0, 1, 0),
                event(3, 120, 4, 1, 0, 0, 1, 0)]
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            summarize_trace(trace(rows))

    def test_guest_packet_identity_and_reuse(self):
        rows = [event(1, 10, 13, 128, 1024, 1280, 720),
                event(2, 20, 6, 128, 1024, 1280, 720),
                event(3, 30, 13, 128, 1024, 1280, 720),
                event(4, 40, 6, 128, 1024, 1280, 720)]
        matched, errors = guest_submits(rows)
        self.assertEqual({key: item['seq'] for key, item in matched.items()}, {2: 1, 4: 3})
        self.assertFalse(errors)
        rows[1]['qpc'] = 35
        matched, errors = guest_submits(rows)
        self.assertFalse(matched)
        self.assertEqual(errors[0]['reason'], 'ambiguous_packet_reuse')

    def test_same_surface_not_same_packet(self):
        matched, errors = guest_submits([event(1, 10, 13, 128, 1024, 1280, 720),
                                        event(2, 20, 6, 256, 1024, 1280, 720)])
        self.assertFalse(matched)
        self.assertEqual(errors[0]['reason'], 'absent_guest_submit')

    def test_guest_observation_mismatch_is_exposed(self):
        result = summarize_trace(trace([event(1, 10, 10, 50, 0x1000, 7),
                                        event(2, 20, 10, 0, 0x2000, 0x1234, 0x5678, 51)]))
        self.assertEqual(result['guest_observations']['counts'], {50: 1, 0: 1})
        self.assertEqual(result['guest_observations']['invalid_sites'][0]['site_id'], 51)
        self.assertIn('input_to_present', result['unmeasured'])

    def test_input_cadence_is_separate_from_latency(self):
        result = summarize_trace(trace([event(1, 100, 3, 0), event(2, 110, 4, 1, 0, 0),
                                        event(3, 200, 3, 0), event(4, 210, 4, 3, 0, 0)]))
        self.assertEqual(result['input_api_return_intervals'][0]['median_ms'], .1)
        self.assertFalse(result['keyboard_responses'])


if __name__ == '__main__':
    unittest.main()
