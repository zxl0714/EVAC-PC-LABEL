import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('capture', Path(__file__).resolve().parents[1]/'scripts/ps4/analyze_capture.py')
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureTests(unittest.TestCase):
    @staticmethod
    def present(sequence=1, start=100, thread=7):
        return dict(event='native_gpu_window_present', sequence=sequence, copy_tick=sequence*10,
                    thread=thread, began_clock_us=start, retired_clock_us=start+10,
                    acquired_clock_us=start+30, recorded_clock_us=start+60,
                    submitted_clock_us=start+100, queued_clock_us=start+150, clock_us=start+210)

    def test_present_parts_are_separate_from_guest_handoff(self):
        item = capture.derive(self.present())
        self.assertTrue(item['ordered'])
        self.assertEqual(item['parts_us'], dict(setup_and_retire=10, acquire=20, record=30,
                                              submit=40, queue_present=50, composite_fence=60))
        item = capture.derive(dict(event='native_gpu_snapshot_handoff', began_clock_us=100,
            submitted_clock_us=120, paced_clock_us=300, copied_clock_us=310, clock_us=315))
        self.assertEqual(item['parts_us'], dict(prepare_and_submit=20, pacing=180, copy_fence=10, publish=5))

    def test_sequence_gaps_resets_and_invalid_order(self):
        stream = capture.TimingStream('queued_clock_us')
        self.assertIsNone(stream.add(self.present()))
        self.assertEqual(stream.add(self.present(3, 20100)), 20000)
        self.assertIsNone(stream.add(self.present(1, 40100)))
        self.assertIsNone(stream.add(self.present(2, 60100, thread=8)))
        bad = self.present(3, 80100, thread=8)
        bad['acquired_clock_us'] = 0
        self.assertIsNone(stream.add(bad))
        self.assertIsNone(stream.add(self.present(4, 100100, thread=8)))
        report = stream.report()
        self.assertEqual((report['sequenceGaps'], report['resets'], report['invalidOrder']), (1, 2, 1))
        self.assertEqual(report['metrics']['interval']['count'], 1)
        self.assertEqual(report['metrics']['interval']['mean_us'], 20000)
        self.assertNotIn('consecutive_interval', report['metrics'])

    def test_analyzer_keeps_window_queue_tails_without_guest_frame_events(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'log.jsonl'
            path.write_text('\n'.join(json.dumps(self.present(i, i*20000)) for i in range(1, 5)))
            report = capture.analyze(path, top=1)
        self.assertEqual(report['frames'], 0)
        self.assertEqual(report['presentation']['native_gpu_window_present']['metrics']['interval']['count'], 3)
        self.assertEqual(len(report['incidents']), 1)
        self.assertEqual(report['incidents'][0]['kind'], 'window_queue_tail')

    def test_handoff_partitions_wall_time_without_adding_cpu_twice(self):
        item = capture.derive(dict(event='native_gpu_worker_handoff', began_clock_us=100,
            enqueued_clock_us=120, started_clock_us=180, finished_clock_us=500,
            clock_us=700, worker_cpu_100ns=1000))
        self.assertTrue(item['ordered'])
        self.assertEqual(item['parts_us'], dict(enqueue=20, queued=60, operation=320, caller_resume=200))
        self.assertEqual(sum(item['parts_us'].values()), 600)
        self.assertEqual(item['worker_cpu_us'], 100)

    def test_late_reservation_and_already_satisfied_wait(self):
        item = capture.derive(dict(event='native_pacing_reserve', target_qpc=900,
                                   qpc=1000, qpc_frequency=10000000))
        self.assertEqual(item['target_in_us'], -10)
        item = capture.derive(dict(event='native_pacing_wait', began_clock_us=100,
            locked_clock_us=110, clock_us=112, notified_clock_us=20,
            completed_counter=12, target_counter=10))
        self.assertEqual(item['resume_after_notification_us'], 2)
        self.assertEqual(item['extra_boundaries'], 2)


if __name__ == '__main__':
    unittest.main()
