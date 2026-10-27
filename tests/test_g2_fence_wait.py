"""ROM-free wiring checks; raw instructions and actual playback are separate."""
from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]

class FenceWaitContracts(unittest.TestCase):
    def test_hook_keeps_original_code_and_only_default_module(self):
        manifest = tomllib.loads((ROOT/'games/espgaluda2/recomp/espgaluda2.toml').read_text())
        self.assertIn('fence-wait-overrides.toml', manifest['entrypoint']['includes'])
        for module in manifest['modules']:
            self.assertNotIn('fence-wait-overrides.toml', module['includes'])
        hooks = tomllib.loads((ROOT/'games/espgaluda2/recomp/fence-wait-overrides.toml').read_text())
        self.assertEqual(hooks['midasm_hook'], [dict(address=0x82173B78,
            name='G2WaitGpuFence', registers=['context','memory'])])

    def test_preserves_predicate_and_original_wait_policy(self):
        code = (ROOT/'games/espgaluda2/performance/fence_wait.cpp').read_text()
        for part in ('device + 10896', 'device + 10908', 'ctx.r30.u32',
                     'GetPhysicalAddress(address)', 'TranslatePhysical(physical)',
                     '__builtin_bswap32(completed)', 'Fallback("bounded_wait_failed")'):
            self.assertIn(part, code)
        for part in ('REX_STORE', 'frame_delay', 'Sleep(', 'jump_address'):
            self.assertNotIn(part, code)
        host = (ROOT/'games/espgaluda2/host.cpp').read_text()
        self.assertIn('performance::StartFenceWait()', host)
        self.assertIn('performance::StopFenceWait()', host)
        helper = (ROOT/'games/espgaluda2/performance/fence_wait.h').read_text()
        self.assertIn('TranslateVirtual<const volatile std::uint32_t*>(address)', helper)
        self.assertNotIn('base + address', code)

    def test_notify_follows_original_completion_store(self):
        code = (ROOT/'third_party/rexglue/src/graphics/command_processor.cpp').read_text()
        fn = code.split('bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_SHD',1)[1].split('\n}',1)[0]
        self.assertLess(fn.index('memory::store('), fn.index('NotifyGuestMemoryWord('))
        self.assertLess(fn.index('NotifyGuestMemoryWord('), fn.index('trace_writer_.WriteMemoryWrite'))
        self.assertIn('NotifyGuestMemoryWord(memory_->TranslatePhysical(address))', fn)

if __name__ == '__main__':
    unittest.main()
