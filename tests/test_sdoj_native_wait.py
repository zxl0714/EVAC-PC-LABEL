"""ROM-free hooks/lifecycle contracts; concurrency is covered by guest_fence_wait."""
from pathlib import Path
import tomllib
import unittest
ROOT=Path(__file__).resolve().parents[1]

class NativeWaitContracts(unittest.TestCase):
    def test_hooks_are_scoped_and_do_not_replace_code(self):
        config=tomllib.loads((ROOT/'games/sdoj/recomp/sdoj.toml').read_text())
        self.assertIn('native-wait-overrides.toml',config['entrypoint']['includes'])
        for module in config['modules']:
            self.assertNotIn('native-wait-overrides.toml',module['includes'])
        hooks=tomllib.loads((ROOT/'games/sdoj/recomp/native-wait-overrides.toml').read_text())
        self.assertEqual(set(hooks),{'midasm_hook'})
        self.assertEqual({h['address'] for h in hooks['midasm_hook']},
            {0x8219D9B4,0x8219DA84,0x8219CD08,0x8215298C,0x8219DB18,0x82215758})
        self.assertTrue(all(h['registers']==['context','memory'] for h in hooks['midasm_hook']))

    def test_original_flag_count_and_type_guard(self):
        code=(ROOT/'games/sdoj/performance/native_wait.cpp').read_text()
        for part in ('0x8279C770','load(load(ctx.r27.u32) + 68) != 0x8219DE60',
                     'callback_wait.Wait(address, stored)', 'pending_wait.Wait(address, stored)',
                     'if (!stored) return;',
                     'reason=bounded_wait_failed','reason=pending_wait_failed'):
            self.assertIn(part,code)
        for forbidden in ('REX_STORE','ctx.r3.u32 =','jump_address','Sleep('):
            # Comments name the native primitive, but code never invokes it.
            lines='\n'.join(line for line in code.splitlines() if not line.lstrip().startswith('//'))
            if forbidden=='Sleep(': continue
            self.assertNotIn(forbidden,lines)
        host=(ROOT/'games/sdoj/host.cpp').read_text()
        self.assertIn('performance::StartNativeWait()',host)
        self.assertLess(host.index('performance::StopNativeWait()'),host.index('sdoj::latency::Stop()'))

    def test_diagnostic_disabled_and_bounded(self):
        code=(ROOT/'games/sdoj/performance/native_wait.cpp').read_text()
        self.assertIn('sdoj_poll_callers, false',code)
        self.assertIn('std::array<std::uint32_t, 16>',code)
        self.assertIn('if (count == seen.size()) return;',code)

    def test_module_tick_wakes_after_stores_and_before_policy_checks(self):
        hooks=tomllib.loads((ROOT/'games/sdoj/recomp/frame-delay-module.toml').read_text())['midasm_hook']
        selected={h['address']:h['name'] for h in hooks if h['name'].startswith('SdojWaitNative') or h['name']=='SdojNativeTickInterrupted'}
        self.assertEqual(selected,{0x880332D0:'SdojWaitNativeTick',
            0x88030A8C:'SdojNativeTickInterrupted',0x880316A4:'SdojNativeTickInterrupted'})
        code=(ROOT/'games/sdoj/latency/module.cpp').read_text()
        body=code.split('bool SdojDelayDriverPeripheral(',1)[1].split('\n}',1)[0]
        self.assertLess(body.index('native_tick_wait.Notify()'),body.index('return early_driver'))
        self.assertIn('!Word(base,ctx.r28.u32+4)',code)
        self.assertIn('Word(base,0x886B41EC+Offset),ctx.r29.u32,ctx.r30.s32',code)
        self.assertEqual(code.count('native_tick_wait.Stop();'),2)
        helper=(ROOT/'backends/xbox360/performance/signal_wait.h').read_text()
        self.assertLess(helper.index('const auto sequence=sequence_.load'),helper.index('!pending()'))

    def test_long_native_completion_recovers_only_at_its_original_producer(self):
        code=(ROOT/'games/sdoj/performance/native_wait.cpp').read_text()
        self.assertNotIn('enabled.exchange(false)',code)
        self.assertIn('callback_wait.Changed(base + kCallbackFlag)',code)
        self.assertIn('pending_wait.Changed(base + kCallbackFlag + 4)',code)
        self.assertIn('pending_timeouts.fetch_add(1) == 0',code)

if __name__=='__main__': unittest.main()
