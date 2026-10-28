"""Native SDOJ changes packet counters: preserve the actual original pass join."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/latency'))
from evac_sdoj_latency import analyze, merge_sdoj_points
from review_sdoj_native_pixels import reviewed_response
from test_evac_futari_latency import CLOCK
from test_latency_probe import event, trace


def fixture():
    raw = dict(trace([event(1, 10, 14, 101, 0x4008, 1),
        event(2, 20, 3, 0), event(3, 25, 15, 1, 0, 1, 0x4008),
        event(4, 30, 4, 2, 0, 0, 1, 0x4008), event(5, 140, 14, 101, 0x4008, 0)]), capacity=100)
    log = '''[t10] SDOJ_INPUT phase=poll_result frame=7 user=0 status=00000000 valid=true buttons=4008 packet=77 t_us=35
[t10] SDOJ_INPUT phase=engine_input_pass frame=7 began_us=15 t_us=40
[t10] SDOJ_INPUT phase=core_mask_built module=1 generation=2 current0=00000018 current1=00000000 t_us=50
[t10] SDOJ_LATENCY phase=logic_copy seq=1 host_iteration=1 module=1 generation=2 argument=888752F0 valid=true swap_stable=true current0=00000018 current1=00000000 thread=10 t_us=60
[t30] SDOJ_LATENCY phase=guest_submit seq=2 host_iteration=1 packet=00000080 valid=true thread=30 t_us=70
[t40] OUTPUT_LATENCY phase=swap frame=7 packet=00000080 frontbuffer=00000100 t_us=80 not_scanout
[t40] OUTPUT_LATENCY phase=ready frame=7 packet=00000080 mailbox=2 t_us=90 active=true not_scanout
[t40] OUTPUT_LATENCY phase=present frame=7 packet=00000080 paint=5 begin_us=100 t_us=110 hr=00000000 guest_drawn=true not_scanout'''
    frame = dict(first_result_output=True, lineage=dict(copy=dict(seq=1),
        began=dict(argument=0x10000), swap=dict(swap_serial=1)),
        present=dict(paint=5, begin_us=100, t_us=110), submit=dict(seq=2), intervals_us={})
    lineage = dict(lineage_summary=dict(continuous_trace=True), frames=[frame])
    return raw, [log], CLOCK, lineage


class NativeSdojLatencyTests(unittest.TestCase):
    def test_original_pass_bridges_rewritten_game_packet(self):
        result = analyze(*fixture())
        self.assertEqual(result['paired_sources'], 1)
        self.assertEqual(result['samples'][0]['native_poll']['packet'], 77)
        self.assertEqual(result['samples'][0]['mapped_buttons'], 0x18)
        self.assertEqual(result['statistics']['game_input_to_present_begin_us']['median_ms'], .04)

    def test_wrong_pass_thread_is_not_a_nearby_sample(self):
        raw, logs, clock, lineage = fixture()
        logs = [logs[0].replace('[t10] SDOJ_INPUT phase=engine_input_pass',
                                '[t11] SDOJ_INPUT phase=engine_input_pass')]
        self.assertEqual(analyze(raw, logs, clock, lineage)['paired_sources'], 0)

    def test_first_core_without_output_is_not_replaced(self):
        raw, logs, clock, lineage = fixture()
        logs = [logs[0].replace('phase=logic_copy seq=1', 'phase=logic_copy seq=9') + '\n' +
            '[t10] SDOJ_LATENCY phase=logic_copy seq=1 host_iteration=1 module=1 generation=2 argument=888752F0 valid=true swap_stable=true current0=00000018 current1=00000000 thread=10 t_us=65']
        result = analyze(raw, logs, clock, lineage)
        self.assertEqual(result['paired_sources'], 0)
        self.assertEqual(result['issues']['first_core_has_no_owned_output'], 1)

    def test_wrong_physical_submit_packet_rejects_selected_output(self):
        raw, logs, clock, lineage = fixture()
        logs = [logs[0].replace('seq=2 host_iteration=1 packet=00000080',
                                'seq=2 host_iteration=1 packet=00000084')]
        result = analyze(raw, logs, clock, lineage)
        self.assertEqual(result['paired_sources'], 0)
        self.assertEqual(result['issues']['selected_output_packet_not_owned'], 1)

    def test_incomplete_capture_or_lineage_rejected(self):
        raw, logs, clock, lineage = fixture()
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            analyze(dict(raw, complete=False), logs, clock, lineage)
        with self.assertRaisesRegex(ValueError, 'Continuous'):
            analyze(raw, logs, clock, dict(lineage, lineage_summary=dict(continuous_trace=False)))

    def review_fixture(self):
        raw, logs, clock, lineage = fixture()
        combined = merge_sdoj_points(raw, logs, clock)
        chain = analyze(raw, logs, clock, lineage)
        present = next(r for r in combined['records'] if r['kind'] == 8)
        capture = dict(header=dict(pid=1, frequency=1000000), footer=dict(overflow=0),
            frames=[{} for _ in range(16)] + [dict(source_event=5, source_packet=0x80, present_begin=present['qpc'])])
        trackers = [dict(examined=[dict(command=101, frame_indexes=list(range(13, 20)),
            owned_index=16, x=[100, 100, 100, 107, 117, 127, 137])]) for _ in range(2)]
        return combined, chain, capture, trackers

    def test_review_is_one_manual_response_not_35_ncc_passes(self):
        result = reviewed_response(*self.review_fixture())
        self.assertEqual(result['accepted_responses'], 1)
        self.assertIsNone(result['p95_ms'])
        self.assertIn('not an automated', result['method'])

    def test_review_different_packet_or_moving_baseline_rejected(self):
        args = self.review_fixture()
        args[2]['frames'][16]['source_packet'] += 4
        with self.assertRaisesRegex(ValueError, 'owned Present'):
            reviewed_response(*args)
        args = self.review_fixture()
        args[3][0]['examined'][0]['x'][1] += 5
        with self.assertRaisesRegex(ValueError, 'positions'):
            reviewed_response(*args)


if __name__ == '__main__':
    unittest.main()
