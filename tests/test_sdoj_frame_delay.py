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
                    0x88044978: 'SdojDelayInputStart',
                    0x880332D0: 'SdojWaitNativeTick',
                    0x88030A8C: 'SdojNativeTickInterrupted',
                    0x880316A4: 'SdojNativeTickInterrupted'}
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
        self.assertEqual(manifest['entrypoint']['includes'][-2:],
                         ['frame-delay-host.toml', 'native-wait-overrides.toml'])
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

    def test_unparsed_parked_batch_defers_before_bootstrap_and_early_input(self):
        adapter = (SOURCE/'latency/module.cpp').read_text(encoding='utf-8')
        early = adapter[adapter.index('void SdojFrameDelayAdvance('):]
        guard = early.index('!render_metrics.Sampled(handoff.parked()->logic)')
        for operation in ('SdojDelayPublish(ctx,base)', 'low.Begin(',
                          'ResolveIndirectFunction(0x88044960)'):
            self.assertLess(guard, early.index(operation))
        self.assertIn('draw_before_early_wake', early[:early.index('SdojDelayPublish(ctx,base)')])

    def test_draw_recovery_freezes_metrics_and_leaves_native_peripheral_tail(self):
        adapter = (SOURCE/'latency/module.cpp').read_text(encoding='utf-8')
        early = adapter[adapter.index('void SdojFrameDelayAdvance('):]
        self.assertLess(early.index('draw_recovery.Reset()'), early.index('draw_recovery.Arm('))
        late = adapter[adapter.index('bool SdojDelayDriverBegin('):adapter.index('bool SdojDelayDriverPeripheral(')]
        self.assertLess(late.index('low.ConsumeDriver('), late.index('draw_recovery.Defer('))
        self.assertLess(late.index('low.SuppressWriter()'), late.index('draw_recovery.Defer('))
        self.assertIn('render_metrics.CanResolve(wrapper_metrics->logic)', late)
        self.assertIn('handoff.parked()->logic==wrapper_metrics->logic', late)
        self.assertIn('wrapper_metrics->deferred=WrapperMetrics::Deferred{native.frame,native.calls}', late)
        self.assertNotIn('render_metrics.DeferWake(', late)
        self.assertIn('effective==1 && handoff.active()', late)
        self.assertIn('control && !Word(base,control+472)', late)
        self.assertIn('ctx.r31.u64=0;return true;', late)
        peripheral = adapter.split('bool SdojDelayDriverPeripheral(', 1)[1].split('\n}', 1)[0]
        self.assertLess(peripheral.index('native_tick_wait.Notify();'),
                        peripheral.index('return early_driver;'))
        self.assertNotIn('early_driver=', peripheral)

    def test_metrics_identity_pinned_at_parser_return_but_values_finalized_at_native_stores(self):
        adapter = (SOURCE/'latency/module.cpp').read_text(encoding='utf-8')
        end = adapter[adapter.index('void SdojDelayReadEnd('):adapter.index('static void Publish(')]
        self.assertIn('if constexpr(Module==1)', end)
        self.assertIn('Word(base,renderer+544),Word(base,renderer+548)', end)
        self.assertLess(end.index('render_metrics.EndRead()'), end.index('handoff.ReadEnd()'))
        self.assertNotIn('render_metrics.Capture(', end)
        stores = adapter[adapter.index('void SdojDelayRenderMetrics('):adapter.index('void SdojDelaySlowdownInputs(')]
        self.assertIn('const RenderMetrics::Pair values{ctx.r11.u32,ctx.r10.u32}', stores)
        capture = stores.index('render_metrics.Capture(wrapper_metrics->logic,values)')
        self.assertLess(capture, stores.index('render_metrics.DeferWake(pending.frame,pending.calls)'))
        self.assertIn('native.frame!=pending.frame || native.calls!=pending.calls', stores)
        self.assertIn('wrapper_metrics->renderer!=native.renderer', stores)
        self.assertIn('wrapper_metrics.reset()', stores)


if __name__ == '__main__':
    unittest.main()
