"""Keep periodic guest allocation tracing off the synchronous CRT stream."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeLoggingTests(unittest.TestCase):
    def test_unpaced_diagnostic_is_explicit_offline_and_separate_from_guest_clocks(self):
        video = (ROOT / 'backends/ps4/native_video.cpp').read_text(encoding='utf-8')
        probe = (ROOT / 'backends/ps4/native_probe.cpp').read_text(encoding='utf-8')
        launch = (ROOT / 'backends/ps4/collection_launch.cpp').read_text(encoding='utf-8')
        self.assertIn('pacing_context{state.active.get(),state.diagnostic_unpaced,state.active->clock->ReserveFlipOpportunity()}', video)
        self.assertIn('(context.unpaced || context.job->clock->WaitFlipOpportunity(context.opportunity))', video)
        self.assertIn('enabled && state.display.vsync', video)
        self.assertIn('collection->shared.community_enabled || collection->shared.vsync', probe)
        self.assertIn('filename().wstring().starts_with(L"benchmark-")', probe)
        self.assertIn('diagnosticUnpacedPresentation', launch)
        self.assertIn('CAVE_DIAGNOSTIC_UNPACED', launch)
        self.assertIn('!configuration.shared.community_enabled && !configuration.shared.vsync && profile.starts_with("benchmark-")', launch)
        # This is not permission to accelerate time APIs or fabricate vblank.
        self.assertIn('clock && clock->WaitVblank()', video)
        self.assertNotIn('diagnostic_unpaced', (ROOT / 'backends/ps4/native_display_clock.cpp').read_text(encoding='utf-8'))

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
