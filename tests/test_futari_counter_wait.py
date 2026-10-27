"""Small ROM-free contracts for the native polling replacement."""
from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CounterWaitContracts(unittest.TestCase):
    def test_only_native_poll_and_post_store_hooks(self):
        hooks = tomllib.loads((ROOT / 'games/futari/recomp/counter-wait-overrides.toml').read_text())['midasm_hook']
        self.assertEqual([(x['address'], x['name']) for x in hooks], [
            (0x8216AD48, 'FutariVblankCounterChanged'),
            (0x8216B244, 'FutariWaitVblank1'),
            (0x8216B2E4, 'FutariWaitVblank0')])
        for hook in hooks:
            self.assertEqual(set(hook), {'address', 'name', 'registers'})
            self.assertEqual(hook['registers'], ['context', 'memory'])

    def test_keeps_native_operands_and_has_no_guest_store(self):
        code = (ROOT / 'games/futari/performance/counter_wait.cpp').read_text()
        self.assertIn('kCounter = 0x821F8560', code)
        self.assertIn('Wait(base, ctx.r10.u64)', code)
        self.assertIn('Wait(base, ctx.r11.u64)', code)
        self.assertIn('__builtin_bswap64(expected)', code)
        self.assertNotIn('REX_STORE', code)
        self.assertNotIn('jump_address', code)

    def test_host_lifecycle_and_generation_are_wired(self):
        host = (ROOT / 'games/futari/host.cpp').read_text()
        self.assertIn('futari::performance::StartCounterWait()', host)
        self.assertIn('futari::performance::StopCounterWait()', host)
        manifest = tomllib.loads((ROOT / 'games/futari/recomp/futari.toml').read_text())
        self.assertIn('counter-wait-overrides.toml', manifest['entrypoint']['includes'])
        for module in manifest['modules']:
            self.assertNotIn('counter-wait-overrides.toml', module.get('includes', []))

if __name__ == '__main__':
    unittest.main()
