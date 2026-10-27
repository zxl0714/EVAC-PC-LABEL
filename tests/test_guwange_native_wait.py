"""ROM-free wait wiring; original-instruction audit and gameplay are separate."""
from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GuwangeWaitContracts(unittest.TestCase):
    def test_hooks_preserve_original_instructions_and_overlay_scope(self):
        manifest = tomllib.loads((ROOT / 'games/guwange/recomp/guwange.toml').read_text())
        self.assertIn('native-wait-overrides.toml', manifest['entrypoint']['includes'])
        for module in manifest['modules']:
            self.assertNotIn('native-wait-overrides.toml', module['includes'])
        hooks = tomllib.loads((ROOT / 'games/guwange/recomp/native-wait-overrides.toml').read_text())
        self.assertEqual(set(hooks), {'midasm_hook'})
        self.assertEqual({h['address']: h['name'] for h in hooks['midasm_hook']}, {
            0x8221C030: 'GuwangePollCaller', 0x821F74AC: 'GuwangeWaitCallback',
            0x821F6A30: 'GuwangeCallbackChanged', 0x821E96B4: 'GuwangeWaitPending',
            0x821F7504: 'GuwangePendingChanged',
            0x821F6E78: 'GuwangeWaitRenderQueue',
            0x821FDF74: 'GuwangeRenderWorkChanged',
            0x821F7708: 'GuwangeRenderWorkChanged',
            0x821F7810: 'GuwangeRenderWorkChanged',
            0x821EBB88: 'GuwangeRenderWorkChanged',
            0x82208ACC: 'GuwangeRenderWorkChanged',
        })
        self.assertTrue(all(h['registers'] == ['context', 'memory'] for h in hooks['midasm_hook']))

    def test_only_known_nonzero_native_predicates_are_parked(self):
        code = (ROOT / 'games/guwange/performance/native_wait.cpp').read_text()
        for fragment in ('0x82C0D578', '0x82C0D938', 'if(!stored) return;',
                         'Load(base,Load(base,ctx.r31.u32)+68)!=0x821F7868',
                         'wait.Wait(ptr,stored)', 'callback.Changed(base,kCallbackFlag)',
                         'pending.Changed(base,kPendingCount)'):
            self.assertIn(fragment, code)
        for fragment in ('REX_STORE', 'ctx.r3.u32 =', 'jump_address', 'frame_delay'):
            self.assertNotIn(fragment, code)
        # Both titles use the tested per-producer timeout/rearm mechanism.
        self.assertIn('backends/xbox360/performance/word_wait.h', code)
        alias = (ROOT / 'games/sdoj/performance/word_wait.h').read_text()
        self.assertIn('using WordWait = cave::xbox360::performance::WordWait;', alias)

    def test_queue_wait_covers_all_original_work_and_stop(self):
        code = (ROOT / 'games/guwange/performance/native_wait.cpp').read_text()
        for fragment in ('Load(base,0x82C0D974)', 'Load(base,cleanup+4)',
                         'ctx.r28.u32+12', 'ctx.r28.u32+16',
                         'ctx.r30.u32+12', 'ctx.r30.u32+16',
                         'Load(base,ctx.r23.u32)', 'Load(base,ctx.r25.u32+44)',
                         'Load(base,Load(base,ctx.r25.u32)+12)!=0x8220D948',
                         'guwange::performance::RenderQueueIdle',
                         'queue_wait.Wait(idle)', 'queue_wait.Stop()', 'queue_wait.Notify()'):
            self.assertIn(fragment, code)

    def test_stop_and_optional_diagnostics(self):
        code = (ROOT / 'games/guwange/performance/native_wait.cpp').read_text()
        body = code.split('void StopNativeWait()', 1)[1].split('\n}', 1)[0]
        self.assertLess(body.index('enabled.store(false'), body.index('callback.Stop('))
        self.assertIn('NotifyGuestMemoryWord(word)', code)
        self.assertIn('guwange_poll_callers, false', code)
        self.assertIn('std::array<std::uint32_t, 16>', code)
        self.assertIn('if (count == seen.size()) return;', code)
        host = (ROOT / 'games/guwange/host.cpp').read_text()
        self.assertIn('performance::StartNativeWait()', host)
        body = host.split('void OnGameStop()', 1)[1]
        self.assertLess(body.index('performance::StopNativeWait()'), body.index('if ('))


if __name__ == '__main__':
    unittest.main()
