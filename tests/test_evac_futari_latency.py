import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from evac_futari_latency import merge_output
from latency_probe import summarize_trace
from test_latency_probe import event, trace
from test_futari_latency import fixture
from futari_latency import analyze

CLOCK = dict(valid=True, samples=1000, frequency=1000000, failures=0,
             mapping='floor(qpc*1000000/frequency)')
LOG = '''[t40] OUTPUT_LATENCY phase=swap frame=7 packet=00000080 frontbuffer=00000100 t_us=20 not_scanout
[t40] OUTPUT_LATENCY phase=ready frame=7 packet=00000080 mailbox=2 t_us=30 active=true not_scanout
[t40] OUTPUT_LATENCY phase=present frame=7 packet=00000080 paint=5 begin_us=40 t_us=50 hr=00000000 guest_drawn=true not_scanout'''


class EvacFutariLatencyTests(unittest.TestCase):
    def native(self):
        return dict(trace([event(1, 10, 13, 128, thread=10), event(2, 100, 1)]), capacity=100)

    def test_exact_packet_and_output_frame_join(self):
        merged = merge_output(self.native(), [LOG], CLOCK)
        report = summarize_trace(merged)
        self.assertFalse(report['invalid_links'])
        self.assertFalse(report['guest_submit_pairing_errors'])
        self.assertEqual(report['output_responses'][0]['guest_submit_seq'], 1)
        self.assertEqual(report['output_responses'][0]['guest_submit_to_present_us'], 30)

    def test_wrong_clock_not_fitted_to_log(self):
        for field, value in [('valid', False), ('samples', 0), ('frequency', 10), ('failures', 1), ('mapping', 'estimated')]:
            with self.assertRaisesRegex(ValueError, 'clock check'):
                merge_output(self.native(), [LOG], dict(CLOCK, **{field: value}))

    def test_lost_rotated_prefix_and_duplicate_logs_rejected(self):
        for logs in [[LOG.split('\n', 1)[1]], [LOG, LOG]]:
            with self.assertRaises(ValueError):
                merge_output(self.native(), logs, CLOCK)

    def test_wrong_packet_is_not_next_present(self):
        report = summarize_trace(merge_output(self.native(), [LOG.replace('00000080', '00000084')], CLOCK))
        self.assertTrue(report['guest_submit_pairing_errors'])
        self.assertIsNone(report['output_responses'][0]['guest_submit_seq'])

    def test_failed_present_not_accepted(self):
        report = summarize_trace(merge_output(self.native(), [LOG.replace('hr=00000000', 'hr=80000001')], CLOCK))
        self.assertEqual(report['output_responses'], [])

    def test_teardown_after_trace_is_counted_not_mispaired(self):
        merged = merge_output(self.native(), [LOG + '\n[t40] OUTPUT_LATENCY phase=swap frame=8 packet=00000090 frontbuffer=00000100 t_us=200'], CLOCK)
        self.assertEqual(merged['output_log_outside_probe_interval'], 1)
        self.assertFalse(summarize_trace(merged)['guest_submit_pairing_errors'])

    def test_native_adapter_preserves_full_game_handshake(self):
        rows = [r for r in fixture() if r['kind'] not in (6, 7, 8, 9)]
        submit = next(r for r in rows if r['kind'] == 13)
        submit.update(b=0, c=0, d=0)
        log = LOG.replace('t_us=20', 't_us=210').replace('t_us=30', 't_us=220')
        log = log.replace('begin_us=40', 'begin_us=230').replace('t_us=50', 't_us=240')
        merged = merge_output(dict(trace(rows), capacity=100), [log], CLOCK)
        report = analyze(merged)
        self.assertEqual(report['paired_sources'], 1)
        self.assertEqual(report['statistics']['source_to_present_begin_us']['median_ms'], 0.22)


if __name__ == '__main__':
    unittest.main()
