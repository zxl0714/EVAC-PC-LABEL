from pathlib import Path
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from analyze_run import analyze, distribution


class RunAnalysisTests(unittest.TestCase):
    def test_balanced_lifecycle_and_markers(self):
        result = analyze(["MODULE_LOAD path='game:/dat15.xex'\n", 'USER_MARKER key=F8\n',
                          "MODULE_UNLOAD path='\\Device\\dat15.xex'\n"])
        self.assertEqual(result['modules'], {'dat15.xex': {'loads': 1, 'unloads': 1}})
        self.assertEqual(result['activeModules'], [])
        self.assertEqual(result['lifecycleWarnings'], [])
        self.assertEqual(result['userMarkers'], 1)

    def test_incomplete_and_duplicate_lifecycle_are_visible(self):
        result = analyze(["MODULE_UNLOAD path='dat00.xex'", "MODULE_LOAD path='dat15.xex'",
                          "MODULE_LOAD path='dat15.xex'"])
        self.assertEqual(result['activeModules'], ['dat15.xex'])
        self.assertEqual(len(result['lifecycleWarnings']), 2)

    def test_output_samples_are_separate_by_driver_and_shutdown_is_excluded(self):
        result = analyze(['AUDIO_DIAG stream driver=1 queue_ms=42.7 silence_blocks=0\n',
                          'AUDIO_DIAG stream driver=2 queue_ms=10 oldest_ms=3\n',
                          'AUDIO_DIAG stream driver=1 queue_ms=44.0\n',
                          'User requested close\n',
                          'AUDIO_DIAG stream driver=1 queue_ms=0 silence_blocks=100\n'])
        self.assertEqual(result['audio']['1']['queue_ms']['samples'], 2)
        self.assertEqual(result['audio']['1']['queue_ms']['maximum'], 44.0)
        self.assertEqual(result['audio']['1']['silence_blocks']['maximum'], 0)
        self.assertEqual(result['audio']['2']['queue_ms']['samples'], 1)
        self.assertTrue(result['closeRequested'])

    def test_invalid_samples_and_empty_data_are_not_zero_measurements(self):
        result = analyze(['AUDIO_DIAG stream driver=1 queue_ms=nan oldest_ms=-1 sdl_input_ms=oops'])
        self.assertEqual(dict(result['audio'].get('1', {})), {})
        self.assertIsNone(distribution([]))
        self.assertIn('not guest sound-command latency', result['measurementScope'])

    def test_errors_and_fatal_events_do_not_imply_clean_log_completion(self):
        result = analyze(['[error] sample failure\n', '[critical] fatal exception\n'])
        self.assertEqual(result['errorLevelLines'], 1)
        self.assertEqual(len(result['fatalLines']), 1)
        self.assertIn('Unknown', result['logCompleteness'])


if __name__ == '__main__':
    unittest.main()
