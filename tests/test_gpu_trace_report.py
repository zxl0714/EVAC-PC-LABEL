"""ROM/GPU-free checks for the developer benchmark report."""
import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from gpu_trace_report import compare, distribution, summarize, paired_overwrite


class TraceReportTests(unittest.TestCase):
    def fixture(self, root, *, missing_frame=None, missing_submission=None,
                dropped=0, invalid=0, changed_image=False, bad_phase=False,
                settings=None):
        root.mkdir()
        rows = []
        interleaved = settings and settings.get('alternate_resolve')
        count = 12 if interleaved else 4
        for i in range(count):
            native = int(i % 4 in (1, 2)) if interleaved else 0
            rows.append(dict(iteration=i, phase='warmup' if i < (4 if interleaved else 1) else 'measured',
                             rgb_hash='bbb' if changed_image and i == 3 else 'aaa',
                             width=32, height=16, restore_us=10, packets_us=20,
                             primary_end_us=3, capture_wall_us=4, restored_bytes=100,
                             reused_bytes=200, native_rgba8=native))
        if bad_phase:
            rows[2]['phase'] = 'warmup'
        with (root / 'replays.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=rows[0])
            writer.writeheader()
            writer.writerows(rows)
        records = []
        submission = 0
        for frame in range(1, count + 1):
            if frame == missing_frame:
                continue
            for draws in (0, 8):
                submission += 1
                if submission == missing_submission:
                    continue
                work = 3 if rows[frame - 1]['native_rgba8'] else 5
                records.append(f'GPU_WORK submission={submission} frame={frame} '
                    f'gpu_us={2 if draws == 0 else work} encode_us=1 draws={draws} copies=0 '
                    f'closes_frame={"true" if draws else "false"} dropped={dropped} invalid={invalid}')
        (root / 'gpu.log').write_text('\n'.join(records))
        if settings is not None:
            (root / 'settings.json').write_text(json.dumps(settings))
        return root

    def test_separates_snapshot_from_guest_and_cpu_restore(self):
        with tempfile.TemporaryDirectory() as directory:
            report = summarize(self.fixture(Path(directory) / 'run'))
        self.assertEqual(report['gpu_us']['snapshot_submission']['mean'], 2)
        self.assertEqual(report['gpu_us']['guest_work_submissions']['mean'], 5)
        self.assertEqual(report['cpu_us']['packets_us']['mean'], 20)
        self.assertEqual(report['reused_bytes_per_iteration']['mean'], 200)

    def test_only_last_gpu_frame_can_be_pending(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(summarize(self.fixture(root / 'last', missing_frame=4))
                             ['gpu_us']['guest_work_submissions']['count'], 2)
            with self.assertRaisesRegex(ValueError, 'Missing GPU frame'):
                summarize(self.fixture(root / 'middle', missing_frame=2))

    def test_incomplete_dropped_and_invalid_gpu_samples_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            for i, args in enumerate((dict(missing_submission=4), dict(dropped=1), dict(invalid=1))):
                with self.subTest(args=args), self.assertRaises(ValueError):
                    summarize(self.fixture(Path(directory) / str(i), **args))

    def test_pixels_and_phases_cannot_change(self):
        with tempfile.TemporaryDirectory() as directory:
            for i, args in enumerate((dict(changed_image=True), dict(bad_phase=True))):
                with self.subTest(args=args), self.assertRaises(ValueError):
                    summarize(self.fixture(Path(directory) / str(i), **args))

    def test_comparison_allows_fast_path_toggle_not_different_work(self):
        settings = dict(trace_xxh3='1234', trace_bytes=50, reuse_memory=True,
                        copy_repetitions=32, native_rgba8=False)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before = self.fixture(root / 'before', settings=settings)
            after = self.fixture(root / 'after', settings=settings | dict(native_rgba8=True))
            self.assertEqual(len(compare([before, after])), 2)
            for i, update in enumerate((dict(trace_xxh3='abcd'), dict(trace_bytes=51),
                                       dict(reuse_memory=False), dict(copy_repetitions=1),
                                       dict(profile_cpu_work=True))):
                other = self.fixture(root / str(i), settings=settings | update)
                with self.subTest(update=update), self.assertRaisesRegex(ValueError, 'settings differ'):
                    compare([before, other])
            legacy = self.fixture(root / 'legacy')
            with self.assertRaisesRegex(ValueError, 'settings missing'):
                compare([before, legacy])

    def test_invalid_values_rejected(self):
        for values in ([], [-1], [float('nan')], [float('inf')]):
            with self.subTest(values=values), self.assertRaises(ValueError):
                distribution(values)

    def test_interleaved_groups_are_paired_without_pending_tail(self):
        settings = dict(alternate_resolve=True)
        with tempfile.TemporaryDirectory() as directory:
            report = summarize(self.fixture(Path(directory) / 'run', settings=settings,
                                            missing_frame=12))
        self.assertEqual(report['resolve_variants']['0']['gpu_us']['mean'], 5)
        self.assertEqual(report['resolve_variants']['1']['gpu_us']['mean'], 3)
        self.assertEqual(report['interleaved']['groups'], 1)
        self.assertAlmostEqual(report['interleaved']['reduction_percent'], 40)

    def test_overwrite_pairs_preserve_wall_and_gpu_units(self):
        rows = [dict(iteration=i, overwrite_upload=str(int(i % 4 in (1,2))),
                     packets_us=80 if i % 4 in (1,2) else 100) for i in range(8)]
        work = {i+1: 50 for i in range(8)}
        result = paired_overwrite(rows, rows, set(range(1,8)), work)
        self.assertEqual(result['packets_wall_us']['groups'], 1)
        self.assertAlmostEqual(result['packets_wall_us']['mean_reduction_percent'], 20)
        self.assertEqual(result['gpu_work_us']['mean_reduction_percent'], 0)
        rows[1]['overwrite_upload'] = '0'
        with self.assertRaisesRegex(ValueError, 'ABBA order'):
            paired_overwrite(rows, rows, set(range(1,8)), work)


if __name__ == '__main__':
    unittest.main()
