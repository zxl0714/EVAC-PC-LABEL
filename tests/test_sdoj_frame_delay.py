from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'games/sdoj'


class FrameDelayContractTests(unittest.TestCase):
    def test_complete_context_hooks_are_last_and_keep_native_frame_wait(self):
        manifest = tomllib.loads((SOURCE / 'recomp/sdoj.toml').read_text(encoding='utf-8'))
        common = tomllib.loads((SOURCE / 'recomp/frame-delay-module.toml').read_text(encoding='utf-8'))
        expected = {0x8803125C: 'SdojDelayPublish', 0x88033210: 'SdojDelayPark',
                    0x880340D8: 'SdojDelayCommands', 0x88033304: 'SdojDelayUnpark',
                    0x88031288: 'SdojDelayRead', 0x880313C0: 'SdojDelayReadEnd',
                    0x88030DA0: 'SdojDelayDriverBegin', 0x88030DF0: 'SdojDelayDriverPeripheral',
                    0x88044978: 'SdojDelayInputStart'}
        self.assertEqual({hook['address']: hook['name'] for hook in common['midasm_hook']}, expected)
        for hook in common['midasm_hook']:
            self.assertEqual(hook['registers'], ['context', 'memory'])
            self.assertFalse(hook.get('return'))
        for module, copy, swap in [('CA022100', 0x88051D74, 0x88051394),
                                   ('CA022110', 0x88051CFC, 0x8805130C)]:
            record = next(item for item in manifest['modules'] if item['guest_path'] == module+'.bin')
            self.assertEqual(record['includes'][-1], module+'-frame-delay.toml')
            policy = tomllib.loads((SOURCE / 'recomp' / record['includes'][-1]).read_text(encoding='utf-8'))
            self.assertEqual(policy['includes'], ['frame-delay-module.toml'])
            prefix, *hooks = policy['midasm_hook']
            self.assertEqual(prefix['address'], 0x880451D4)
            self.assertEqual(prefix['name'], 'SdojDelayInputDone')
            self.assertEqual(prefix['jump_address_on_true'], 0x88045DC0 if module=='CA022100' else 0x88045D70)
            expected_hooks=[(copy, 'SdojDelayLogic'), (swap, 'SdojDelayLateSwap')]
            if module=='CA022100':
                expected_hooks += [(0x880315C8,'SdojDelayRenderMetrics'),
                                   (0x88123E44,'SdojDelaySlowdownInputs'),
                                   (0x880387B0,'SdojDelayMetricsParse')]
            self.assertEqual([(hook['address'], hook['name']) for hook in hooks],expected_hooks)
            self.assertEqual(hooks[1]['jump_address_on_true'], swap+4)
            self.assertTrue(all(hook['registers'] == ['context', 'memory'] for hook in hooks))
        self.assertEqual(manifest['entrypoint']['includes'][-1], 'frame-delay-host.toml')
        host = tomllib.loads((SOURCE/'recomp/frame-delay-host.toml').read_text(encoding='utf-8'))
        self.assertEqual(host['midasm_hook'], [dict(address=0x8211F0DC, name='SdojDelayAdvance',registers=['context','memory'])])
        self.assertFalse({0x8219D9B0, 0x8219D9C8} & expected.keys())

    def test_diagnostic_composition_and_full_native_swap(self):
        adapter = (SOURCE / 'latency/module.cpp').read_text(encoding='utf-8')
        for name in ('SdojCoreNormalCopy(ctx.r27)', 'SdojCoreArrangeCopy(ctx.r25)',
                     'SdojLatencyNormalCommandsBegin(ctx.r11)', 'SdojLatencyArrangeCommandsBegin(ctx.r11)',
                     'SdojRenderNormalWaitEnd()', 'SdojRenderArrangeWaitEnd()',
                     'SdojLatencyNormalSource(ctx.r4)', 'SdojLatencyArrangeSource(ctx.r4)',
                     'SdojLatencyNormalRenderEnd()', 'SdojLatencyArrangeRenderEnd()'):
            self.assertIn(name, adapter)
        self.assertIn('ResolveIndirectFunction(0x88033070)(ctx, base)', adapter)
        self.assertNotIn('REX_STORE_', adapter)
        self.assertIn('ctx.r1.u32 + 136', adapter)
        self.assertIn('handoff.Park(native, ctx.r29.u32, ctx.r30.u32, sentinel)', adapter)

    def test_per_guest_load_lifetime_not_native_pe_constructor(self):
        kernel = (ROOT/'third_party/rexglue/src/system/kernel_state.cpp').read_text(encoding='utf-8')
        self.assertIn('GetRawSymbol("ReXModule_OnLoad")', kernel)
        self.assertIn('GetRawSymbol("ReXModule_OnUnload")', kernel)
        adapter = (SOURCE/'latency/module.cpp').read_text(encoding='utf-8')
        self.assertIn('ReXModule_OnLoad()', adapter)
        self.assertIn('ReXModule_OnUnload()', adapter)
        self.assertIn('handoff.Retire()', adapter)
        self.assertIn('handoff.Load(policy)', adapter)
        host = (SOURCE/'latency/host.cpp').read_text(encoding='utf-8')
        self.assertIn('GetProcAddress(module, "SdojFrameDelayStop")', host)
        self.assertIn('void SdojFrameDelayStop()', adapter)

    def test_unknown_original_metrics_defer_early_logic_without_skipping_native_draw(self):
        adapter = (SOURCE/'latency/module.cpp').read_text(encoding='utf-8')
        early = adapter[adapter.index('void SdojFrameDelayAdvance('):]
        guard = early.index('if (!render_metrics.OriginalInputs())')
        self.assertLess(guard, early.index('SdojDelayPublish(ctx,base)'))
        self.assertLess(guard, early.index('low.Begin('))
        self.assertLess(guard, early.index('ResolveIndirectFunction(0x88044960)'))
        self.assertIn('low_metrics_skips', early[:early.index('SdojDelayPublish(ctx,base)')])
        self.assertIn('missing original slowdown metrics', adapter)


if __name__ == '__main__':
    unittest.main()
