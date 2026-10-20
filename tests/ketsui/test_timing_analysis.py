"""Timing reports use authored JSON events, never commercial game data."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('kt_timing_analysis', ROOT / 'scripts/ps4/analyze_timing.py')
TIMING = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TIMING)


class TimingAnalysisTests(unittest.TestCase):
    def command_frame(self, **changes):
        stages = dict(duration_us=1000, draw_us=300, acquire_us=500, prefetch_us=50,
                      flush_us=100, other_us=50, acquires=2, prefetches=1)
        stages.update(changes)
        frame = self.frame(1000000, 19000, command_stages=stages)
        frame['execute_us'] = 1100
        return frame

    def test_command_scopes_include_drains_and_handoff_without_double_count(self):
        report = self.report([self.command_frame()])
        self.assertEqual(report['counts']['command_timing_samples'], 1)
        timing = report['command_substages']['timing']
        self.assertEqual(timing['acquire_us']['max_us'], 500)
        self.assertEqual(timing['outer_overhead_us']['max_us'], 100)

    def test_command_scopes_reject_invalid_sum_types_or_parent(self):
        frames = [self.command_frame(**change) for change in
                  (dict(duration_us=999), dict(draw_us=True), dict(acquire_us=-1),
                   dict(acquires='2'), dict(duration_us=1102, other_us=152))]
        report = self.report(frames + [self.frame(1000000, 19000)])
        self.assertEqual(report['counts']['invalid_command_timing'], 5)
        self.assertEqual(report['command_substages']['timing']['duration_us']['count'], 0)

    def test_command_scopes_obey_scene_window_and_rounding(self):
        report = self.report([self.command_frame(duration_us=1101, other_us=151)], to_clock_us=1000000)
        self.assertEqual(report['command_substages']['timing']['outer_overhead_us']['max_us'], 0)
        report = self.report([self.command_frame()], from_clock_us=1000001)
        self.assertEqual(report['command_substages']['timing']['duration_us']['count'], 0)

    def backend_frame(self, **changes):
        stages = dict(completed=True, duration_us=3000, prepare_us=10, retire_us=10,
                      acquire_us=20, record_us=100, submit_us=50, queue_present_us=10,
                      gpu_wait_us=200, present_wait_us=2500, finalize_us=100)
        stages.update(changes)
        return self.frame(1000000, 19000, present_stages=dict(duration_us=7000, lock_wait_us=1,
            prepare_us=2, display_clock_wait_us=3900, backend_present_us=3096, completion_us=1),
            backend_stages=stages)

    def test_backend_fence_and_worker_scopes_are_validated_without_double_count(self):
        report = self.report([self.backend_frame()])
        self.assertEqual(report['counts']['backend_present_timing_samples'], 1)
        timing = report['backend_present_substages']['timing']
        self.assertEqual(timing['present_wait_us']['max_us'], 2500)
        self.assertEqual(timing['gpu_wait_us']['max_us'], 200)
        self.assertEqual(timing['outer_overhead_us']['max_us'], 96)
        self.assertEqual(timing['duration_us']['count'], 1)

    def test_invalid_or_absent_backend_scopes_are_not_evidence(self):
        samples = [self.backend_frame(**change) for change in
                   (dict(duration_us=3001), dict(gpu_wait_us=True), dict(acquire_us=-1),
                    dict(duration_us=3098, present_wait_us=2598))]
        report = self.report(samples + [self.backend_frame(completed=False), self.frame(1000000, 19000)])
        self.assertEqual(report['counts']['invalid_backend_present_timing'], 4)
        self.assertEqual(report['counts']['unmeasured_backend_frames'], 1)
        self.assertEqual(report['backend_present_substages']['timing']['duration_us']['count'], 0)
        report = self.report([self.backend_frame()], from_clock_us=1000001)
        self.assertEqual(report['backend_present_substages']['timing']['duration_us']['count'], 0)

    def present(self, **kwargs):
        item = dict(event='native_video_present_timing', began_clock_us=950000, clock_us=999990,
                    duration_us=49990, thread=11, handle=1, buffer=0,
                    lock_wait_us=10, prepare_us=20, display_clock_wait_us=17000,
                    backend_present_us=32950, completion_us=10, swapchain_retries=0, zero_extent_polls=0)
        item.update(kwargs)
        return item

    def test_slow_present_stages_are_validated_and_correlated(self):
        report = self.report([self.present(), self.frame(1000000, 60000)])
        self.assertEqual(report['counts']['slow_present_timing_samples'], 1)
        self.assertEqual(report['present_substages']['timing']['display_clock_wait_us']['max_us'], 17000)
        self.assertEqual(report['present_substages']['timing']['backend_present_us']['max_us'], 32950)
        self.assertTrue(any(item['event']['event'] == 'native_video_present_timing'
                            for item in report['longest_frames'][0]['nearby_events']))

    def test_sub_20ms_completed_frame_keeps_all_present_stages(self):
        stages = dict(duration_us=7000, lock_wait_us=1, prepare_us=2,
                      display_clock_wait_us=5000, backend_present_us=1996, completion_us=1)
        report = self.report([self.frame(1000000, 19000, present_stages=stages), self.present()])
        self.assertEqual(report['counts']['frame_present_timing_samples'], 1)
        self.assertEqual(report['counts']['slow_present_timing_samples'], 1)
        timing = report['all_frame_present_substages']['timing']
        self.assertEqual(timing['display_clock_wait_us']['max_us'], 5000)
        self.assertEqual(timing['backend_present_us']['max_us'], 1996)
        self.assertEqual(timing['duration_us']['count'], 1)

    def test_invalid_frame_scopes_are_not_zero_or_unbounded_evidence(self):
        valid = dict(duration_us=7000, lock_wait_us=1, prepare_us=2,
                     display_clock_wait_us=5000, backend_present_us=1996, completion_us=1)
        samples = [self.frame(1000000, 19000, present_stages=dict(valid, **change))
                   for change in (dict(duration_us=6999), dict(lock_wait_us=True),
                                  dict(backend_present_us=-1), dict(duration_us=7002, completion_us=3))]
        samples += [self.frame(1000000, 19000, present_stages=None)]
        report = self.report(samples + [self.frame(1000000, 19000)])
        self.assertEqual(report['counts']['invalid_frame_present_timing'], 5)
        self.assertEqual(report['all_frame_present_substages']['timing']['duration_us']['count'], 0)
        self.assertEqual(report['counts']['completed_frames'], 6)

    def test_frame_present_scope_rounding_and_clock_filter(self):
        stages = dict(duration_us=7001, lock_wait_us=1, prepare_us=2,
                      display_clock_wait_us=5000, backend_present_us=1997, completion_us=1)
        report = self.report([self.frame(1000000, 19000, present_stages=stages),
                              self.frame(2000000, 19000, present_stages=stages)], to_clock_us=1000000)
        self.assertEqual(report['counts']['frame_present_timing_samples'], 1)
        self.assertEqual(report['all_frame_present_substages']['timing']['duration_us']['max_us'], 7001)

    def test_invalid_or_out_of_window_present_stages_are_not_evidence(self):
        samples = [self.present(**change) for change in
                   (dict(duration_us=1), dict(display_clock_wait_us=-1), dict(thread=True),
                    dict(buffer=16), dict(completion_us=11))]
        report = self.report(samples + [self.frame(1000000, 60000)])
        self.assertEqual(report['counts']['invalid_present_timing'], 5)
        self.assertEqual(report['present_substages']['timing']['duration_us']['count'], 0)
        report = self.report([self.present(), self.frame(1000000, 60000)], from_clock_us=1000000)
        self.assertEqual(report['present_substages']['timing']['duration_us']['count'], 0)
        self.assertTrue(any(item['event']['event'] == 'native_video_present_timing'
                            for item in report['longest_frames'][0]['nearby_events']))

    def log_snapshot(self, **kwargs):
        sample = dict(event='native_async_log', clock_us=2000000, capacity_records=1024, record_bytes=4096,
                      accepted=20, written=18, dropped_oldest=1, contended=2, invalid_records=0,
                      format_failures=0, sink_failures=0, after_stop=0, pending=0, in_flight=1,
                      sink_wall_max_us=150000, stop_requested=False, drain_completed=False)
        sample.update(kwargs)
        return sample

    def test_async_logging_keeps_lifetime_loss_and_drain_outside_clock_filter(self):
        report = self.report([dict(event='native_async_log_begin'), self.log_snapshot(),
            self.frame(1000000, 17000), self.log_snapshot(clock_us=3000000, written=19, in_flight=0,
                                                       stop_requested=True, drain_completed=True)],
            from_clock_us=990000, to_clock_us=1010000)
        summary = report['async_logging']
        self.assertTrue(summary['observed'])
        self.assertEqual(len(summary['snapshots']), 2)
        self.assertEqual(summary['latest']['contended'], 2)
        self.assertEqual(summary['latest']['dropped_oldest'], 1)
        self.assertEqual(summary['latest']['sink_wall_max_us'], 150000)
        self.assertTrue(summary['latest']['drain_completed'])
        self.assertEqual(report['counts']['completed_frames'], 1)

    def test_async_logging_snapshots_are_bounded_and_not_summed(self):
        report = self.report([self.log_snapshot(accepted=index, written=0) for index in range(100)])
        self.assertEqual(len(report['async_logging']['snapshots']), 8)
        self.assertEqual(report['async_logging']['latest']['accepted'], 99)
        self.assertEqual(report['counts']['async_log_diagnostic_snapshots_omitted'], 92)

    def test_absent_async_logging_does_not_claim_zero_loss_or_a_drain(self):
        report = self.report([self.frame(1000000, 17000)])
        self.assertFalse(report['async_logging']['observed'])
        self.assertIsNone(report['async_logging']['latest'])

    def test_invalid_async_log_counters_do_not_become_evidence(self):
        samples = [self.log_snapshot(**change) for change in
                   (dict(written=True), dict(pending=-1), dict(clock_us='clock'),
                    dict(capacity_records=0), dict(drain_completed=1))]
        report = self.report(samples)
        self.assertEqual(report['counts']['invalid_async_log_diagnostics'], 5)
        self.assertIsNone(report['async_logging']['latest'])

    def report(self, events, **kwargs):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'stdout.jsonl'
            path.write_text(''.join(json.dumps(item) + '\n' for item in events), encoding='utf-8')
            return TIMING.analyze(path, **kwargs)

    def frame(self, clock, interval, **kwargs):
        return dict(event='native_gnm_frame_completed', completed_clock_us=clock, completion_interval_us=interval,
                    total_us=10000, snapshot_us=100, queue_wait_us=0, execute_us=2900, present_us=7000, **kwargs)

    def frame_counters(self, clock=1000000, interval=50000, **kwargs):
        groups = {key: dict(calls=0, total_us=0, max_us=0) for key in TIMING.HOST_CATEGORIES}
        groups['memory'] = dict(calls=2, total_us=7000, max_us=5000)
        groups['allocation'] = dict(calls=1, total_us=10000, max_us=10000)
        result = dict(event='native_host_frame_timing', submission=7, clock_us=clock, interval_us=interval,
                      counters_began_clock_us=clock-interval, counters_began_submission=6,
                      thread=11, same_thread_interval=True,
                      active_scopes_at_boundary=0, cross_boundary_completed_scopes=0,
                      bounded_to_interval=True, outer_calls=1, outer_us=10000, categories=groups)
        result.update(kwargs)
        return result

    def test_frame_distributions_and_bounded_longest_list(self):
        report = self.report([self.frame(100000, 0), self.frame(150000, 50000), self.frame(168000, 18000),
                              self.frame(208000, 40000)], top_count=1)
        self.assertEqual(report['counts']['completed_frames'], 4)
        self.assertEqual(report['counts']['long_frames'], 2)
        self.assertEqual(report['frame_timing']['completion_interval_us']['count'], 3)
        self.assertEqual(report['frame_timing']['completion_interval_us']['median_upper_us'], 40000)
        self.assertEqual(report['frame_timing']['completion_interval_us']['max_us'], 50000)
        self.assertEqual(report['frame_timing']['between_submissions_us']['max_us'], 40000)
        self.assertEqual(len(report['longest_frames']), 1)
        self.assertEqual(report['longest_frames'][0]['frame']['completed_clock_us'], 150000)

    def test_audio_and_delayed_underrun_report_use_distinct_clock_points(self):
        report = self.report([self.frame(1000000, 70000),
            dict(event='native_audio_timing', stream=1, clock_us=1500000, underruns=2,
                 last_underrun_clock_us=990000, max_submit_gap_us=60000),
            dict(event='native_ngs2_render_completed', system=7, clock_us=1010000,
                 render_us=40000, graph_wait_us=35000, registry_wait_us=100, starved_grains=3)], correlation_us=20000)
        nearby = report['longest_frames'][0]['nearby_events']
        self.assertEqual([(e['clock_kind'], e['delta_us']) for e in nearby], [('last_underrun', -10000), ('sample', 10000)])
        self.assertEqual(report['audio_streams']['1']['max_underruns'], 2)
        self.assertEqual(report['ngs2']['max_graph_wait_us'], 35000)
        self.assertEqual(report['ngs2']['starved_grains_by_system'], {'7': 3})

    def test_old_audio_logs_do_not_acquire_fabricated_timestamps(self):
        report = self.report([self.frame(1000000, 70000),
            dict(event='native_ngs2_render_completed', system=7, grain=1, active_sources=1),
            dict(event='native_audio_close', underruns=10)])
        self.assertEqual(report['ngs2']['timed_samples'], 0)
        self.assertEqual(report['audio_streams'], {})
        self.assertEqual(report['longest_frames'][0]['nearby_events'], [])

    def test_stream_counters_are_per_stream_and_cumulative_not_summed(self):
        report = self.report([dict(event='native_audio_timing', stream=stream, clock_us=clock, underruns=underruns)
                              for stream, clock, underruns in ((1, 10, 2), (1, 20, 2), (2, 30, 5), (1, 40, 3))])
        self.assertEqual(report['audio_streams']['1']['max_underruns'], 3)
        self.assertEqual(report['audio_streams']['1']['samples'], 3)
        self.assertEqual(report['audio_streams']['2']['max_underruns'], 5)

    def test_f8_markers_share_clock_window_and_frame_context(self):
        marker = lambda identity, clock: dict(event='native_timeline_marker', id=identity, clock_us=clock, source='F8')
        report = self.report([marker(1, 900000), marker(2, 1000000), self.frame(1000010, 60000),
                              marker(3, 1100000)], from_clock_us=1000000, to_clock_us=1000100)
        self.assertEqual(report['timeline_markers'], [dict(line=2, id=2, clock_us=1000000, source='F8')])
        self.assertEqual(report['counts']['timeline_markers'], 1)
        self.assertEqual(report['longest_frames'][0]['nearby_events'][0]['event']['id'], 2)

    def test_diagnostic_writes_are_separate_not_subtracted_or_fabricated(self):
        frame = self.frame(1000000, 650000, diagnostic_log_us=400000, previous_frame_log_us=200000, host_thread_id=11)
        report = self.report([frame, self.frame(1016667, 16667)])
        self.assertEqual(report['frame_timing']['diagnostic_log_us']['count'], 1)
        self.assertEqual(report['frame_timing']['diagnostic_log_us']['max_us'], 400000)
        self.assertEqual(report['frame_timing']['previous_frame_log_us']['max_us'], 200000)
        self.assertEqual(report['longest_frames'][0]['frame']['total_us'], 10000)
        self.assertEqual(report['longest_frames'][0]['frame']['host_thread_id'], 11)

    def test_f8_markers_are_bounded_and_invalid_markers_are_not_listed(self):
        events = [dict(event='native_timeline_marker', id=i + 1, clock_us=i, source='F8') for i in range(300)]
        events += [dict(event='native_timeline_marker', id=True, clock_us=1, source='F8'),
                   dict(event='native_timeline_marker', id=301, clock_us=-1, source='F8'),
                   dict(event='native_timeline_marker', id=302, source='F8')]
        report = self.report(events)
        self.assertEqual(len(report['timeline_markers']), 256)
        self.assertEqual(report['counts']['timeline_markers'], 300)
        self.assertEqual(report['counts']['timeline_markers_omitted'], 44)
        self.assertEqual(report['counts']['invalid_timeline_markers'], 3)

    def test_empty_malformed_and_live_partial_lines_are_explicit(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'stdout.jsonl'
            path.write_text('\ufeffnull\n{"event":9}\nnot-json\n{"event":', encoding='utf-8')
            result = TIMING.analyze(path)
        self.assertEqual(result['counts']['malformed_lines'], 4)
        self.assertIsNone(result['frame_clock_span_us'])
        self.assertIsNone(result['frame_timing']['total_us']['p95_upper_us'])

    def test_histogram_precision_overflow_and_input_bounds(self):
        distribution = TIMING.Distribution()
        for value in (0, 101, 10000001):
            distribution.add(value)
        report = distribution.report()
        self.assertEqual(report['median_upper_us'], 200)
        self.assertIsNone(report['p99_upper_us'])
        self.assertEqual(report['max_us'], 10000001)
        self.assertEqual(report['overflow_samples'], 1)
        for value in (-1, True, '100'):
            with self.assertRaises(ValueError):
                distribution.add(value)
        for kwargs in (dict(top_count=0), dict(top_count=257), dict(threshold_us=0), dict(correlation_us=1000001)):
            with self.assertRaises(ValueError):
                self.report([], **kwargs)

    def test_correlation_is_bounded_and_clock_regression_is_visible(self):
        events = [self.frame(1000000, 70000), self.frame(900000, 50000)]
        events += [dict(event='native_ngs2_render_completed', clock_us=1000000, grain=i) for i in range(100)]
        report = self.report(events)
        self.assertEqual(report['frame_clock_regressions'], 1)
        self.assertTrue(all(len(frame['nearby_events']) <= 24 for frame in report['longest_frames']))

    def test_cli_accepts_run_directory_and_preserves_existing_report(self):
        reports = ROOT / '.local/test-reports'
        reports.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='ps4-timing-', dir=reports) as temporary:
            path = Path(temporary)
            (path / 'stdout.jsonl').write_text(json.dumps(self.frame(1000000, 70000)) + '\n', encoding='utf-8')
            output = path / 'timing.json'
            command = [sys.executable, str(ROOT / 'scripts/ps4/analyze_timing.py'), str(path), '--output', str(output)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            before = output.read_bytes()
            repeated = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(repeated.returncode, 0)
            self.assertEqual(output.read_bytes(), before)

    def test_clock_window_separates_loading_and_player_frames_and_is_inclusive(self):
        events = [self.frame(100, 900000), self.frame(1000, 16000), self.frame(2000, 30000),
                  self.frame(3000, 17000), self.frame(4000, 800000)]
        events += [dict(event='native_gnm_frame_completed', completion_interval_us=999999)]
        report = self.report(events, from_clock_us=1000, to_clock_us=3000)
        self.assertEqual(report['counts']['completed_frames'], 3)
        self.assertEqual(report['counts']['long_frames'], 1)
        self.assertEqual(report['counts']['outside_clock_window_samples'], 2)
        self.assertEqual(report['counts']['unclocked_samples_excluded'], 1)
        self.assertEqual(report['frame_clock_span_us'], 2000)
        self.assertEqual(report['frame_timing']['completion_interval_us']['max_us'], 30000)

    def test_filtered_audio_counters_keep_lifetime_semantics_and_delayed_context_is_retained(self):
        report = self.report([self.frame(1000000, 70000),
            dict(event='native_audio_timing', stream=1, clock_us=1000000, underruns=50),
            dict(event='native_audio_timing', stream=2, clock_us=1500000, underruns=9,
                 last_underrun_clock_us=990000)], from_clock_us=980000, to_clock_us=1020000,
                 correlation_us=20000)
        self.assertEqual(report['audio_streams']['1']['max_underruns'], 50)
        self.assertNotIn('2', report['audio_streams'])
        self.assertTrue(any(e['event'].get('stream') == 2 and e['clock_kind'] == 'last_underrun'
                            for e in report['longest_frames'][0]['nearby_events']))

    def test_invalid_clock_bounds_and_old_logs_do_not_fabricate_window_times(self):
        for bounds in (dict(from_clock_us=-1), dict(to_clock_us=True), dict(from_clock_us='1'),
                       dict(from_clock_us=2, to_clock_us=1)):
            with self.assertRaises(ValueError):
                self.report([], **bounds)
        report = self.report([dict(event='native_ngs2_render_completed', render_us=10000)], from_clock_us=0)
        self.assertEqual(report['ngs2']['samples'], 0)
        self.assertEqual(report['counts']['unclocked_samples_excluded'], 1)

    def test_cli_clock_window_is_applied_to_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'stdout.jsonl'
            path.write_text(''.join(json.dumps(item) + '\n' for item in
                (self.frame(100, 900000), self.frame(1000, 17000))), encoding='utf-8')
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/ps4/analyze_timing.py'), str(path),
                '--from-clock-us', '900', '--to-clock-us', '1100'], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report['counts']['completed_frames'], 1)
            self.assertEqual(report['frame_timing']['completion_interval_us']['max_us'], 17000)

    def test_compilation_statistics_and_frame_scope_overlap(self):
        report = self.report([self.frame(1000000, 50000),
            dict(event='native_gpu_compile_begin', id=1, clock_us=800000),
            dict(event='native_gpu_compile_completed', id=1, kind='shader', began_clock_us=800000,
                 clock_us=1300000, duration_us=500000, unwinding=False)], correlation_us=10000)
        self.assertEqual(report['counts']['gpu_compile_scopes'], 1)
        self.assertEqual(report['gpu_compilation']['shader']['max_us'], 500000)
        self.assertEqual(report['longest_compiles'][0]['event']['id'], 1)
        nearby = report['longest_frames'][0]['nearby_events']
        self.assertEqual([(e['clock_kind'], e['delta_us']) for e in nearby], [('scope_overlap', 300000)])

    def test_outer_rack_holds_do_not_double_count_recursion_and_missing_times_stay_unknown(self):
        events = [self.frame(1000000, 50000),
            dict(event='native_ngs2_rack_lock', operation='unlock', depth=1, clock_us=1100000, held_us=0),
            dict(event='native_ngs2_rack_lock', operation='unlock', depth=0, clock_us=1300000,
                 began_clock_us=900000, held_us=400000),
            dict(event='native_ngs2_rack_lock', operation='unlock', depth=0)]
        report = self.report(events, correlation_us=10000)
        self.assertEqual(report['outer_rack_lock_holds']['count'], 1)
        self.assertEqual(report['outer_rack_lock_holds']['max_us'], 400000)
        self.assertEqual(len(report['longest_rack_lock_holds']), 1)
        self.assertTrue(any(e['clock_kind'] == 'scope_overlap' for e in report['longest_frames'][0]['nearby_events']))

    def test_scope_lists_are_bounded_and_window_attributes_whole_duration_at_completion(self):
        events = [dict(event='native_gpu_compile_completed', kind='shader', id=i, began_clock_us=0,
                       clock_us=i, duration_us=i, unwinding=i == 5) for i in range(1, 11)]
        report = self.report(events, top_count=2, from_clock_us=4, to_clock_us=7)
        self.assertEqual(report['gpu_compilation']['shader']['count'], 4)
        self.assertEqual(report['counts']['gpu_compile_unwinding_scopes'], 1)
        self.assertEqual([e['event']['id'] for e in report['longest_compiles']], [7, 6])

    def test_invalid_compile_samples_do_not_create_categories_or_invent_durations(self):
        events = [dict(event='native_gpu_compile_completed', kind='unexpected', duration_us=10),
                  dict(event='native_gpu_compile_completed', kind=[], duration_us=10),
                  dict(event='native_gpu_compile_completed', kind='shader', duration_us=True),
                  dict(event='native_gpu_compile_completed', kind='shader', duration_us=-1)]
        report = self.report(events)
        self.assertEqual(report['counts']['invalid_compile_scope_samples'], 4)
        self.assertEqual(report['gpu_compilation']['shader']['count'], 0)
        self.assertEqual(report['longest_compiles'], [])

    def test_host_operation_categories_and_save_phases_are_separate_whole_scopes(self):
        events = [dict(event='native_host_operation', name='memcpy', category='memory',
                       began_clock_us=100, clock_us=10100, duration_us=10000, thread=7),
                  dict(event='native_save_transaction_staged', directory='SlotA',
                       began_clock_us=100, clock_us=20100, stage_us=20000, copy_bytes=4096),
                  dict(event='native_save_transaction_completed', directory='SlotA', action='commit',
                       began_clock_us=50100, clock_us=80100, total_us=30000),
                  dict(event='native_save_transaction_completed', directory='SlotB', action='no_changes',
                       began_clock_us=80100, clock_us=85100, total_us=5000)]
        report = self.report(events)
        self.assertEqual(report['host_operations']['memory']['max_us'], 10000)
        self.assertEqual(report['counts']['host_operation_scopes'], 1)
        self.assertEqual(report['counts']['save_transaction_scopes'], 3)
        self.assertEqual([report['save_transactions'][key]['max_us'] for key in ('stage', 'commit', 'no_changes')],
                         [20000, 30000, 5000])
        self.assertEqual(report['longest_host_operations'][0]['event']['thread'], 7)
        self.assertEqual([entry['event']['directory'] for entry in report['longest_save_operations']], ['SlotA', 'SlotA', 'SlotB'])

    def test_operation_fully_inside_a_long_frame_is_correlated_without_nearby_endpoints(self):
        report = self.report([self.frame(1000000, 500000),
            dict(event='native_host_operation', name='memcpy', category='memory',
                 began_clock_us=600000, clock_us=800000, duration_us=200000, thread=7)], correlation_us=10000)
        nearby = report['longest_frames'][0]['nearby_events']
        self.assertEqual(len(nearby), 1)
        self.assertEqual((nearby[0]['clock_kind'], nearby[0]['delta_us'], nearby[0]['overlap_us']),
                         ('scope_overlap', -200000, 200000))

    def test_slow_host_and_save_scopes_survive_dense_routine_context(self):
        events = [self.frame(1000000, 500000)]
        events += [dict(event='native_ngs2_render_completed', clock_us=1000000, grain=i) for i in range(100)]
        events += [dict(event='native_host_operation', name='malloc', category='allocation',
                        began_clock_us=600000, clock_us=800000, duration_us=200000),
                   dict(event='native_save_transaction_completed', directory='SlotA', action='commit',
                        began_clock_us=650000, clock_us=900000, total_us=250000)]
        report = self.report(events, correlation_us=10000)
        longest = report['longest_frames'][0]
        self.assertEqual(len(longest['nearby_events']), 24)
        self.assertEqual(longest['nearby_events_omitted'], 78)
        kept = {item['event']['event'] for item in longest['nearby_events']}
        self.assertIn('native_host_operation', kept)
        self.assertIn('native_save_transaction_completed', kept)

    def test_operation_window_counts_completion_but_keeps_overlapping_outside_context(self):
        events = [self.frame(1000000, 400000),
                  dict(event='native_host_operation', name='sleep', category='sleep',
                       began_clock_us=700000, clock_us=900000, duration_us=200000),
                  dict(event='native_host_operation', name='read', category='io',
                       began_clock_us=900000, clock_us=1100000, duration_us=200000),
                  dict(event='native_save_transaction_staged', directory='SlotA',
                       began_clock_us=800000, clock_us=1000000, stage_us=200000)]
        report = self.report(events, from_clock_us=990000, to_clock_us=1010000, correlation_us=0)
        self.assertEqual(report['host_operations']['io']['count'], 0)
        self.assertEqual(report['host_operations']['sleep']['count'], 0)
        self.assertEqual(report['save_transactions']['stage']['count'], 1)
        self.assertEqual(len(report['longest_frames'][0]['nearby_events']), 3)

    def test_host_diagnostic_snapshots_remain_lifetime_and_are_bounded(self):
        events = [dict(event='native_host_timing_diagnostics', admitted=i, suppressed=2*i) for i in range(11)]
        report = self.report(events, from_clock_us=100)
        self.assertEqual(len(report['host_operation_diagnostics']), 8)
        self.assertEqual(report['host_operation_diagnostics'][-1]['event']['admitted'], 10)
        self.assertEqual(report['counts']['host_timing_diagnostic_snapshots_omitted'], 3)

    def test_invalid_operation_and_save_durations_do_not_invent_work(self):
        events = [dict(event='native_host_operation', name='bad', category='memory',
                       began_clock_us=100, clock_us=200, duration_us=value) for value in (-1, True, 99)]
        events += [dict(event='native_host_operation', name='bad', category=[], duration_us=1),
                   dict(event='native_save_transaction_completed', action=[], total_us=1),
                   dict(event='native_save_transaction_staged', began_clock_us=100, clock_us=200, stage_us=99)]
        report = self.report(events)
        self.assertEqual(report['counts']['invalid_host_operation_samples'], 4)
        self.assertEqual(report['counts']['invalid_save_scope_samples'], 2)
        self.assertEqual(report['host_operations']['memory']['count'], 0)
        self.assertEqual(report['save_transactions']['stage']['count'], 0)

    def test_exact_frame_counter_pairing_keeps_nested_categories_separate(self):
        report = self.report([self.frame(1000000, 50000, submission=7, host_thread_id=11), self.frame_counters()])
        attached = report['longest_frames'][0]['host_frame_counters']['event']
        self.assertEqual(attached['outer_us'], 10000)
        self.assertEqual(sum(group['total_us'] for group in attached['categories'].values()), 17000)
        self.assertEqual(report['host_frame_counters']['outer_us']['max_us'], 10000)
        self.assertEqual(report['host_frame_counters']['category_inclusive_us']['memory']['max_us'], 7000)
        self.assertEqual(report['counts']['bounded_host_frame_counter_samples'], 1)

    def test_frame_counter_clock_rounding_requires_consecutive_submissions(self):
        rounded = self.frame_counters(counters_began_clock_us=949999)
        frame = self.frame(1000000, 50000, submission=7, host_thread_id=11)
        report = self.report([frame, rounded])
        self.assertEqual(report['counts'].get('bounded_host_frame_counter_samples'), 1)
        self.assertEqual(report['longest_frames'][0]['host_frame_counters']['event'], rounded)
        for changes in (dict(counters_began_clock_us=949998), dict(counters_began_clock_us=950001),
                        dict(counters_began_submission=5), dict(counters_began_submission=True),
                        dict(counters_began_submission=-1)):
            with self.subTest(changes=changes):
                invalid = dict(rounded, **changes)
                report = self.report([frame, invalid])
                self.assertEqual(report['counts'].get('invalid_host_frame_counters'), 1)
                self.assertNotIn('host_frame_counters', report['longest_frames'][0])
        skipped = dict(rounded, counters_began_submission=5, same_thread_interval=False, bounded_to_interval=False)
        report = self.report([frame, skipped])
        self.assertEqual(report['counts'].get('unbounded_host_frame_counter_samples'), 1)

    def test_legacy_frame_counter_rounding_is_not_silently_reclassified(self):
        legacy = self.frame_counters()
        del legacy['counters_began_submission']
        report = self.report([legacy])
        self.assertEqual(report['counts'].get('bounded_host_frame_counter_samples'), 1)
        legacy.update(counters_began_clock_us=949999, same_thread_interval=False, bounded_to_interval=False)
        report = self.report([legacy])
        self.assertEqual(report['counts'].get('unbounded_host_frame_counter_samples'), 1)

    def test_allocation_operation_breakdown_is_optional_and_preserved_in_exact_frame(self):
        sample = self.frame_counters()
        operations = {key: dict(calls=0, total_us=0, max_us=0) for key in TIMING.HOST_ALLOCATIONS}
        operations['malloc'] = dict(calls=1, total_us=10000, max_us=10000)
        sample['allocation_operations'] = operations
        frame = self.frame(1000000, 50000, submission=7, host_thread_id=11)
        report = self.report([frame, sample])
        self.assertEqual(report['longest_frames'][0]['host_frame_counters']['event']['allocation_operations'], operations)
        self.assertEqual(report['counts'].get('bounded_allocation_operation_counter_samples'), 1)
        self.assertEqual(report['host_frame_counters']['allocation_operation_inclusive_us']['malloc']['max_us'], 10000)
        sample.update(bounded_to_interval=False, active_scopes_at_boundary=1)
        unbounded = self.report([sample])
        self.assertEqual(unbounded['host_frame_counters']['allocation_operation_inclusive_us']['malloc']['count'], 0)
        legacy = self.report([self.frame_counters()])
        self.assertEqual(legacy['host_frame_counters']['allocation_operation_inclusive_us']['malloc']['count'], 0)

    def test_invalid_allocation_breakdown_does_not_become_profiler_data(self):
        import copy
        sample = self.frame_counters()
        sample['allocation_operations'] = {key: dict(calls=0, total_us=0, max_us=0) for key in TIMING.HOST_ALLOCATIONS}
        sample['allocation_operations']['malloc'] = dict(calls=1, total_us=10000, max_us=10000)
        for mode in range(6):
            with self.subTest(mode=mode):
                invalid = copy.deepcopy(sample)
                if mode == 0: invalid['allocation_operations'] = []
                if mode == 1: del invalid['allocation_operations']['unclassified']
                if mode == 2: invalid['allocation_operations']['malloc']['calls'] = True
                if mode == 3: invalid['allocation_operations']['malloc']['calls'] = 2
                if mode == 4: invalid['allocation_operations']['malloc']['max_us'] = 9999
                if mode == 5: invalid['allocation_operations']['malloc']['total_us'] = 9999
                report = self.report([invalid])
                self.assertEqual(report['counts'].get('invalid_host_frame_counters'), 1)
                self.assertEqual(report['host_frame_counters']['allocation_operation_inclusive_us']['malloc']['count'], 0)

    def test_count_only_allocations_keep_exact_counts_but_no_fabricated_zero_duration(self):
        sample = self.frame_counters(allocation_timing='counts', outer_coverage='timed_scopes_only')
        sample['categories']['allocation'].update(total_us=None, max_us=None)
        sample['allocation_operations'] = {key: dict(calls=0, total_us=None, max_us=None) for key in TIMING.HOST_ALLOCATIONS}
        sample['allocation_operations']['malloc']['calls'] = 1
        report = self.report([self.frame(1000000, 50000, submission=7, host_thread_id=11), sample])
        self.assertEqual(report['counts']['bounded_allocation_timing_counts_samples'], 1)
        self.assertEqual(report['longest_frames'][0]['host_frame_counters']['event']['categories']['allocation']['calls'], 1)
        self.assertIsNone(report['longest_frames'][0]['host_frame_counters']['event']['categories']['allocation']['total_us'])
        self.assertEqual(report['host_frame_counters']['category_inclusive_us']['allocation']['count'], 0)
        self.assertEqual(report['host_frame_counters']['allocation_operation_inclusive_us']['malloc']['count'], 0)
        self.assertEqual(report['host_frame_counters']['category_inclusive_us']['memory']['max_us'], 7000)
        self.assertEqual(report['host_frame_counters']['outer_us']['max_us'], 10000)

    def test_count_only_allocation_schema_rejects_zero_masquerading_as_unmeasured(self):
        import copy
        sample = self.frame_counters(allocation_timing='counts')
        sample['categories']['allocation'].update(total_us=None, max_us=None)
        sample['allocation_operations'] = {key: dict(calls=0, total_us=None, max_us=None) for key in TIMING.HOST_ALLOCATIONS}
        sample['allocation_operations']['malloc']['calls'] = 1
        for mode in range(6):
            with self.subTest(mode=mode):
                invalid = copy.deepcopy(sample)
                if mode == 0: invalid['categories']['allocation']['total_us'] = 0
                if mode == 1: invalid['allocation_operations']['malloc']['max_us'] = 0
                if mode == 2: invalid['allocation_timing'] = 'unknown'
                if mode == 3: invalid['allocation_timing'] = 'full'
                if mode == 4: invalid['allocation_operations']['malloc']['calls'] = 2
                if mode == 5: del invalid['categories']['allocation']['max_us']
                report = self.report([invalid])
                self.assertEqual(report['counts'].get('invalid_host_frame_counters'), 1)

    def test_frame_counter_attachment_requires_all_four_identity_fields(self):
        frame = self.frame(1000000, 50000, submission=7, host_thread_id=11)
        for changes in (dict(clock_us=1000001, counters_began_clock_us=950001), dict(submission=8),
                        dict(thread=12), dict(interval_us=40000, counters_began_clock_us=960000)):
            with self.subTest(changes=changes):
                report = self.report([frame, self.frame_counters(**changes)])
                self.assertNotIn('host_frame_counters', report['longest_frames'][0])

    def test_unbounded_cross_frame_counters_stay_visible_but_do_not_become_frame_statistics(self):
        report = self.report([self.frame(1000000, 50000, submission=7, host_thread_id=11),
            self.frame_counters(cross_boundary_completed_scopes=1, bounded_to_interval=False)])
        self.assertFalse(report['longest_frames'][0]['host_frame_counters']['event']['bounded_to_interval'])
        self.assertEqual(report['counts']['unbounded_host_frame_counter_samples'], 1)
        self.assertEqual(report['host_frame_counters']['outer_us']['count'], 0)
        self.assertIsNone(report['host_frame_counters']['outer_us']['max_us'])

    def test_frame_counter_window_and_legacy_absence_do_not_fabricate_samples(self):
        report = self.report([self.frame(1000000, 50000, submission=7, host_thread_id=11), self.frame_counters(),
                              self.frame_counters(clock=2000000)], from_clock_us=990000, to_clock_us=1010000)
        self.assertEqual(report['counts']['host_frame_counter_samples'], 1)
        self.assertEqual(report['counts']['outside_clock_window_samples'], 1)
        legacy = self.report([self.frame(1000000, 50000)])
        self.assertNotIn('host_frame_counters', legacy['longest_frames'][0])
        self.assertEqual(legacy['host_frame_counters']['outer_us']['count'], 0)

    def test_invalid_and_duplicate_counter_samples_are_reported(self):
        changes = (dict(outer_us=True), dict(bounded_to_interval=True, same_thread_interval=False),
                   dict(categories=[]), dict(active_scopes_at_boundary=1),
                   dict(counters_began_clock_us=1), dict(outer_calls=0), dict(outer_calls=4))
        report = self.report([self.frame(1000000, 50000, submission=7, host_thread_id=11)] +
                             [self.frame_counters(**change) for change in changes] +
                             [self.frame_counters(), self.frame_counters(outer_us=9999)])
        self.assertEqual(report['counts']['invalid_host_frame_counters'], 7)
        self.assertEqual(report['counts']['duplicate_host_frame_counters'], 1)
        self.assertEqual(report['longest_frames'][0]['host_frame_counters']['event']['outer_us'], 10000)


if __name__ == '__main__':
    unittest.main()
