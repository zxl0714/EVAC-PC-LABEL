"""GPU timing scopes use authored operations and need no device or game data."""
import json
import os
from pathlib import Path
import subprocess
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'out/gpu-core/KetsuiGpuCompileMetricsSmoke.exe'


@unittest.skipUnless(os.name == 'nt' and EXE.exists(), 'Build-GpuCore.ps1 required')
class CompileMetricsTests(unittest.TestCase):
    def test_scope_pairs_concurrent_ids_common_clock_and_exception_unwinding(self):
        began = time.perf_counter_ns() // 1000
        result = subprocess.run([str(EXE)], capture_output=True, text=True, timeout=10)
        ended = time.perf_counter_ns() // 1000
        self.assertEqual(result.returncode, 0, result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(events), 12)
        starts = {e['id']: e for e in events if e['event'] == 'native_gpu_compile_begin'}
        finishes = {e['id']: e for e in events if e['event'] == 'native_gpu_compile_completed'}
        self.assertEqual(set(starts), set(range(1, 7)))
        self.assertEqual(set(finishes), set(starts))
        self.assertEqual({e['kind'] for e in starts.values()}, {'shader', 'graphics_pipeline', 'compute_pipeline', 'tiling_pipeline'})
        for identity, start in starts.items():
            finish = finishes[identity]
            self.assertEqual((start['kind'], start['key']), (finish['kind'], finish['key']))
            self.assertEqual(finish['began_clock_us'], start['clock_us'])
            self.assertEqual(finish['duration_us'], finish['clock_us'] - start['clock_us'])
            self.assertGreaterEqual(finish['duration_us'], 0)
            # Both independent C++/Python binaries use this host's QPC epoch.
            self.assertGreaterEqual(start['clock_us'], began - 1000)
            self.assertLessEqual(finish['clock_us'], ended + 1000)
            self.assertEqual(finish['unwinding'], start['kind'] == 'graphics_pipeline')


if __name__ == '__main__':
    unittest.main()
