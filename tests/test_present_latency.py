from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from present_latency import analyze, distribution


def row(qpc=100000, **updates):
    return dict(ProcessID='42', SwapChainAddress='0x100', PresentRuntime='DXGI',
                CPUStartQPC=str(qpc), CPUBusy='16', CPUWait='0.5', FrameTime='16.5',
                DisplayLatency='19', DisplayedTime='16.67',
                PresentMode='Composed: Flip', SyncInterval='0',
                PresentFlags='516', AllowsTearing='0') | updates


def report(rows, **updates):
    return analyze(rows, **(dict(process_id=42, start_qpc=100000, end_qpc=1000000,
                               qpc_frequency=10000000) | updates))


class PresentLatencyTests(unittest.TestCase):
    def test_derives_from_same_frame_not_display_duration(self):
        result = report([row()])
        metrics = result['policies'][0]['metrics']
        self.assertEqual(metrics['present_entry_to_os_display_ms']['p50_ms'], 3)
        self.assertEqual(metrics['present_return_to_os_display_signed_ms']['p50_ms'], 2.5)
        self.assertEqual(metrics['present_call_ms']['p50_ms'], .5)
        self.assertIn('not game input', result['scope'])

    def test_display_can_precede_present_return(self):
        result = report([row(CPUWait='4', FrameTime='20')])
        self.assertEqual(result['policies'][0]['metrics']['present_return_to_os_display_signed_ms']['min_ms'], -1)

    def test_na_never_becomes_zero_latency(self):
        result = report([row(), row(300000, DisplayLatency='NA', DisplayedTime='NA')])
        self.assertEqual(result['counts']['accepted_display_rows'], 1)
        self.assertEqual(result['counts']['not_displayed_or_unobserved_rows'], 1)

    def test_all_unobserved_rejected(self):
        with self.assertRaisesRegex(ValueError, 'No observed'):
            report([row(DisplayLatency='NA', DisplayedTime='NA')])

    def test_scene_end_and_other_process_are_excluded(self):
        result = report([row(), row(700000, ProcessID='43'), row(900000)])
        self.assertEqual(result['counts']['accepted_display_rows'], 1)
        self.assertEqual(result['counts']['other_process_rows'], 1)
        self.assertEqual(result['counts']['crosses_scene_end_rows'], 1)

    def test_modes_remain_separate(self):
        result = report([row(), row(300000, PresentMode='Hardware: Independent Flip', AllowsTearing='1')])
        self.assertEqual(len(result['policies']), 2)

    def test_wrong_origin_cannot_be_used_for_vulkan(self):
        with self.assertRaisesRegex(ValueError, 'DXGI instrumentation'):
            report([row(PresentRuntime='Other')])

    def test_multiple_swapchains_rejected(self):
        with self.assertRaisesRegex(ValueError, 'one swapchain'):
            report([row(), row(300000, SwapChainAddress='0x200')])

    def test_repeated_or_generated_frame_rejected(self):
        for data in ([row(), row()], [row(FrameType='Interpolated')]):
            with self.assertRaises(ValueError):
                report(data)

    def test_invalid_numeric_and_timing_pairs_rejected(self):
        for update in (dict(DisplayLatency='NaN'), dict(CPUBusy='-1'),
                       dict(DisplayLatency='15'), dict(DisplayLatency='NA'),
                       dict(CPUWait='1'), dict(FrameTime='inf')):
            with self.subTest(update=update), self.assertRaises(ValueError):
                report([row(**update)])

    def test_missing_columns_rejected(self):
        data = row()
        del data['CPUBusy']
        with self.assertRaisesRegex(ValueError, 'Missing'):
            report([data])

    def test_fractional_percentiles(self):
        stats = distribution([4, 1, 3, 2])
        self.assertEqual(stats['p50_ms'], 2.5)
        self.assertAlmostEqual(stats['p95_ms'], 3.85)

    def test_bad_scene_or_clock_rejected(self):
        for update in (dict(qpc_frequency=0), dict(end_qpc=100000), dict(start_qpc=-1)):
            with self.subTest(update=update), self.assertRaises(ValueError):
                report([row()], **update)


if __name__ == '__main__':
    unittest.main()
