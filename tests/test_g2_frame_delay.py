"""Small contracts for the optional G2 presentation hooks, without game data."""
from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]


class G2FrameDelayTests(unittest.TestCase):
    def test_hooks_leave_native_resolve_and_swap_intact(self):
        folder = ROOT / 'games/espgaluda2/recomp'
        hooks = tomllib.loads((folder / 'frame-delay-overrides.toml').read_text())['midasm_hook']
        self.assertEqual(hooks, [
            dict(address=0x8217064C, name='G2CompletedVblank', registers=['context', 'memory']),
            dict(address=0x821713A4, name='G2PresentCpuWait', registers=['context', 'memory'],
                 jump_address_on_true=0x821713A8),
        ])
        # No whole-function replacement and no extra overlays modified.
        manifest = tomllib.loads((folder / 'espgaluda2.toml').read_text())
        self.assertIn('frame-delay-overrides.toml', manifest['entrypoint']['includes'])
        for module in manifest['modules']:
            self.assertNotIn('frame-delay-overrides.toml', module['includes'])

    def test_host_selects_config_and_stops_waiter(self):
        source = (ROOT / 'games/espgaluda2/host.cpp').read_text()
        self.assertIn('Configure(run().settings.g2_frame_delay)', source)
        stop = source.split('void OnGameStop() override {', 1)[1].split('}', 1)[0]
        self.assertIn('espgaluda2::performance::StopFenceWait();', stop)
        self.assertIn('espgaluda2::latency::Stop();', stop)
        cmake = (ROOT / 'games/espgaluda2/CMakeLists.txt').read_text()
        self.assertIn('target_sources(espgaluda2 PRIVATE latency/host.cpp)', cmake)
        self.assertIn('NAME g2_vblank_gate', cmake)

    def test_default_and_fallback_are_original(self):
        source = (ROOT / 'games/espgaluda2/latency/host.cpp').read_text()
        self.assertIn('enabled.store(mode==1', source)
        self.assertIn('Result::advanced', source)
        self.assertIn('RestorePolicy(base,device);', source)
        self.assertIn('actual=2 action=retain_original_until_restart', source)
        self.assertIn('sub_82174B30(ctx,base)', source)
        self.assertLess(source.index('Drain(ctx,base,device)'), source.index('gate.Wait(device'))
        self.assertIn('ctx.fpscr.setcsr(csr)', source)


if __name__ == '__main__':
    unittest.main()
