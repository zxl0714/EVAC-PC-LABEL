from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from present_frame_join import join, responses


def capture(*frames):
    return dict(header=dict(pid=42, frequency=10000000), footer=dict(overflow=0),
                frames=list(frames) or [frame()])


def frame(index=0, begin=259990, end=265010):
    return dict(index=index, present_begin=begin, present_end=end, hr=0,
                source_frame=10+index, source_packet=20+index, source_event=30+index)


def row(qpc=100000, **updates):
    return dict(ProcessID='42', SwapChainAddress='0x100', PresentRuntime='DXGI',
                CPUStartQPC=str(qpc), CPUBusy='16', CPUWait='.5', FrameTime='16.5',
                DisplayLatency='19', DisplayedTime='16.67', PresentMode='Composed: Flip',
                SyncInterval='0', PresentFlags='516', AllowsTearing='0') | updates


class PresentFrameJoinTests(unittest.TestCase):
    def test_same_call_entry_and_return_are_bounded(self):
        result = join(capture(), [row()])
        image = result['images'][0]
        self.assertEqual(result['counts']['matched_presents'], 1)
        self.assertEqual(image['etw_entry_qpc'], 260000)
        self.assertAlmostEqual(image['display']['present_entry_to_os_display_ms'], 3.001)

    def test_nearest_frame_outside_call_is_not_used(self):
        result = join(capture(frame(begin=270000, end=275000)), [row()])
        self.assertFalse(result['images'])
        self.assertEqual(result['missing'][0]['reason'], 'no_contained_etw_present')

    def test_entry_inside_but_return_outside_is_not_used(self):
        self.assertFalse(join(capture(frame(end=264000)), [row()])['images'])

    def test_na_and_untracked_are_unavailable_not_zero(self):
        for data in [row(DisplayLatency='NA', DisplayedTime='NA'),
                     {k:v for k,v in row().items() if k not in ('DisplayLatency','DisplayedTime')}]:
            result = join(capture(), [data])
            self.assertEqual(result['counts']['matched_presents'], 1)
            self.assertEqual(result['counts']['observed_displays'], 0)
            self.assertIsNone(result['images'][0]['display'])

    def test_one_etw_call_cannot_own_two_images(self):
        with self.assertRaisesRegex(ValueError, 'two captured'):
            join(capture(frame(), frame(index=1)), [row()])

    def test_ambiguous_calls_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            join(capture(frame(end=300000)), [row(), row(110000)])

    def test_wrong_pid_does_not_match_even_with_identical_time(self):
        with self.assertRaisesRegex(ValueError, 'PID'):
            join(capture(), [row(ProcessID='43')])

    def test_runtime_swapchain_and_generated_frames_rejected(self):
        for rows in ([row(PresentRuntime='Other')], [row(FrameType='Interpolated')],
                     [row(), row(300000, SwapChainAddress='0x200')]):
            with self.assertRaises(ValueError):
                join(capture(), rows)

    def test_invalid_timing_and_incomplete_feedback_rejected(self):
        for updates in (dict(DisplayLatency='15'), dict(CPUBusy='nan'),
                        dict(FrameTime='17'), dict(DisplayLatency='NA')):
            with self.subTest(updates=updates), self.assertRaises(ValueError):
                join(capture(), [row(**updates)])

    def test_response_totals_are_per_input_not_sum_of_medians(self):
        result = join(capture(), [row()])
        response = dict(command=101, first_response_index=0,
                        source_to_response_present_us=25000, core_to_response_present_us=17000)
        paired = responses(result, [response])
        self.assertAlmostEqual(paired['samples'][0]['source_to_response_os_display_ms'], 28.001)
        self.assertAlmostEqual(paired['samples'][0]['core_to_response_os_display_ms'], 20.001)
        self.assertIsNone(paired['statistics']['source_to_response_os_display_ms']['p95_ms'])
        missing = responses(join(capture(), [row(DisplayLatency='NA', DisplayedTime='NA')]), [response])
        self.assertFalse(missing['samples'])
        self.assertIsNone(missing['statistics']['source_to_response_os_display_ms'])

    def test_display_before_present_return_is_permitted(self):
        result = join(capture(frame(end=300010)), [row(CPUWait='4', FrameTime='20')])
        self.assertEqual(result['counts']['observed_displays'], 1)

    def test_invalid_response_intervals_rejected(self):
        result = join(capture(), [row()])
        response = dict(command=101, first_response_index=0,
                        source_to_response_present_us=25000, core_to_response_present_us=17000)
        for updates in (dict(source_to_response_present_us=float('nan')),
                        dict(core_to_response_present_us=-1),
                        dict(source_to_response_present_us=1)):
            with self.subTest(updates=updates), self.assertRaises(ValueError):
                responses(result, [response | updates])

    def test_overflow_and_duplicate_etw_rejected(self):
        cap = capture()
        cap['footer']['overflow'] = 1
        with self.assertRaises(ValueError): join(cap, [row()])
        with self.assertRaises(ValueError): join(capture(), [row(), row()])


if __name__ == '__main__':
    unittest.main()
