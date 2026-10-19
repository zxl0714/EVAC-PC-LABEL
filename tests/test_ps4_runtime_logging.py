"""Keep periodic guest allocation tracing off the synchronous CRT stream."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeLoggingTests(unittest.TestCase):
    def test_periodic_allocation_trace_uses_the_bounded_background_sink(self):
        source = (ROOT / 'backends/ps4/native_probe.cpp').read_text(encoding='utf-8')
        body = source.split('void TraceAllocation(', 1)[1].split('\nvoid* KT_SYSV NativeNew(', 1)[0]
        self.assertIn('call <= 32 || call % 1024 == 0 || !pointer', body)
        self.assertIn('NativeLogf(', body)
        for synchronous in ('std::printf(', 'std::puts(', 'std::fwrite(', 'std::fflush('):
            self.assertNotIn(synchronous, body)
        self.assertIn('native_allocation', body)
        self.assertIn('size, pointer, call', body)


if __name__ == '__main__':
    unittest.main()
