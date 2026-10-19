"""Slow-operation diagnostics use authored inputs, no game or device required."""
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'out/native/KetsuiNativeProbe.exe'


@unittest.skipUnless(os.name == 'nt' and EXE.is_file(), 'Build the native Windows probe first')
class HostTimingTests(unittest.TestCase):
    def test_threshold_rate_limits_concurrency_errors_and_scope_unwind(self):
        result = subprocess.run([str(EXE), '--host-timing-self-test'], capture_output=True,
                                text=True, encoding='utf-8', timeout=15, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [item for item in events if item['event'] == 'host_timing_self_test_case']
        self.assertEqual(len(cases), 25)
        self.assertTrue(all(item['passed'] for item in cases), cases)
        scopes = [item for item in events if item['event'] == 'native_host_operation']
        self.assertEqual([item['name'] for item in scopes], ['authored.wait', 'authored.exception'])
        for item in scopes:
            self.assertEqual(item['category'], 'fixture')
            self.assertIsNone(item['caller_image'])
            self.assertGreater(item['thread'], 0)
            self.assertEqual(item['duration_us'], item['clock_us'] - item['began_clock_us'])
            self.assertGreaterEqual(item['duration_us'], 8000)
            self.assertEqual(item['suppressed_since_last_record'], 0)
        summary = next(item for item in events if item['event'] == 'native_host_timing_diagnostics')
        self.assertEqual((summary['threshold_us'], summary['work_thread_second_limit'],
                          summary['work_process_second_limit'], summary['routine_wait_thread_second_limit'],
                          summary['routine_wait_process_second_limit'], summary['long_wait_us']), (8000, 8, 48, 2, 16, 25000))
        self.assertEqual((summary['admitted'], summary['emission_failures'], summary['suppressed'],
                          summary['suppressed_not_yet_reported']), (2, 0, 0, 0))
        self.assertTrue(summary['snapshot_only'])
        self.assertEqual(summary['frame_emission_failures'], 0)
        frames = [item for item in events if item['event'] == 'native_host_frame_timing']
        self.assertEqual(len(frames), 1)
        frame = frames[0]
        self.assertEqual((frame['submission'], frame['clock_us'], frame['interval_us'],
                          frame['counters_began_clock_us'], frame['outer_calls'], frame['outer_us']),
                         (101, 1030000, 30000, 1000000, 4, 17000))
        self.assertTrue(frame['same_thread_interval'])
        self.assertEqual(frame['counters_began_submission'], 100)
        self.assertTrue(frame['bounded_to_interval'])
        self.assertEqual(frame['categories']['memory'], dict(calls=1, total_us=5000, max_us=5000))
        self.assertEqual(frame['categories']['synchronization'], dict(calls=1, total_us=10000, max_us=10000))
        self.assertEqual(frame['categories']['fixture'], dict(calls=0, total_us=0, max_us=0))
        self.assertEqual(frame['categories']['allocation'], dict(calls=2, total_us=2000, max_us=1200))
        self.assertEqual(frame['allocation_timing'], 'full')
        self.assertEqual(next(item for item in events if item['event'] == 'native_host_timing_configuration')['allocation_timing'], 'full')
        self.assertEqual(frame['allocation_operations']['malloc'], dict(calls=1, total_us=1200, max_us=1200))
        self.assertEqual(frame['allocation_operations']['free_delete'], dict(calls=1, total_us=800, max_us=800))
        self.assertEqual(sum(value['calls'] for value in frame['allocation_operations'].values()), 2)
        self.assertEqual(events[-1], dict(event='host_timing_self_test', cases=25, passed=True))

    def test_counts_only_allocations_are_unmeasured_not_zero_and_track_cross_frame_lifetimes(self):
        result = subprocess.run([str(EXE), '--host-timing-counts-self-test'], capture_output=True,
                                text=True, encoding='utf-8', timeout=15, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [item for item in events if item['event'] == 'host_counts_self_test_case']
        self.assertEqual(len(cases), 8)
        self.assertTrue(all(item['passed'] for item in cases), cases)
        frames = [item for item in events if item['event'] == 'native_host_frame_timing']
        self.assertEqual([item['submission'] for item in frames], [101, 201])
        first, crossed = frames
        self.assertTrue(first['bounded_to_interval'])
        self.assertEqual(first['allocation_timing'], 'counts')
        self.assertEqual(first['categories']['allocation'], dict(calls=2, total_us=None, max_us=None))
        self.assertEqual(first['allocation_operations']['malloc'], dict(calls=1, total_us=None, max_us=None))
        self.assertEqual(first['allocation_operations']['free_delete'], dict(calls=1, total_us=None, max_us=None))
        self.assertEqual(first['outer_coverage'], 'timed_scopes_only')
        self.assertEqual(first['outer_calls'], 1)
        self.assertGreater(first['outer_us'], 0)
        self.assertFalse(crossed['bounded_to_interval'])
        self.assertEqual(crossed['cross_boundary_completed_scopes'], 1)
        self.assertEqual(crossed['categories']['allocation']['calls'], 1)
        self.assertFalse(any(item['event'] == 'native_host_operation' and item['category'] == 'allocation' for item in events))
        summary = next(item for item in events if item['event'] == 'native_host_timing_diagnostics')
        self.assertEqual(summary['allocation_timing'], 'counts')
        self.assertEqual(summary['frame_emission_failures'], 0)
        self.assertEqual(events[-1], dict(event='host_counts_self_test', cases=8, passed=True))

    def test_allocation_timing_arguments_are_validated_before_loading_images(self):
        for args in (['--enter', '--allocation-timing', 'invalid'],
                     ['--validate', '--allocation-timing', ''],
                     ['--binding-census', '--allocation-timing', 'counts'],
                     ['--enter', '--allocation-timing', 'full', '--allocation-timing', 'counts'],
                     ['--enter', '--allocation-timing']):
            with self.subTest(args=args):
                result = subprocess.run([str(EXE), 'missing-authored-input.ktprobe', *args], capture_output=True,
                                        text=True, encoding='utf-8', timeout=5, cwd=ROOT)
                self.assertEqual(result.returncode, 2)
                self.assertIn('Usage:', result.stderr)
                self.assertNotIn('native_host_timing_configuration', result.stdout)

    def test_default_is_counts_without_configuration_and_full_remains_explicit(self):
        result = subprocess.run([str(EXE), '--host-timing-default-self-test'], capture_output=True,
                                text=True, encoding='utf-8', timeout=15, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertFalse(any(item['event'] == 'native_host_timing_configuration' for item in events))
        cases = [item for item in events if item['event'] == 'host_counts_self_test_case']
        self.assertEqual(len(cases), 8)
        self.assertTrue(all(item['passed'] for item in cases), cases)
        self.assertEqual(cases[0]['name'], 'counts_mode_is_default')
        frames = [item for item in events if item['event'] == 'native_host_frame_timing']
        self.assertEqual(len(frames), 2)
        self.assertTrue(all(item['allocation_timing'] == 'counts' for item in frames))
        self.assertTrue(all(item['categories']['allocation']['total_us'] is None for item in frames))


if __name__ == '__main__':
    unittest.main()
