import copy
import json
from pathlib import Path
import sys
import tempfile
import tomllib
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import generate_game
from build_receipt import selected_games
from generate_catalogue import load
from project import ROOT


class SdojGenerationTests(unittest.TestCase):
    def setUp(self):
        self.game = next(game for game in load(ROOT / 'games/catalogue.json') if game['id'] == 'sdoj')
        self.manifest = tomllib.loads((ROOT / 'games/sdoj/recomp/sdoj.toml').read_text(encoding='utf-8'))

    def test_three_xex_images_include_two_named_bin_files(self):
        records = generate_game.targets(self.manifest, self.game)
        self.assertEqual(list(records), ['default', 'CA022100', 'CA022110'])
        headers = json.loads((ROOT / 'games/sdoj/inputs.json').read_text(encoding='utf-8'))['xex']
        self.assertEqual({p['path'] for p in headers}, {p['program_path'] for p in records.values()})
        self.assertEqual({p['titleId'] for p in headers}, {'435A07E7'})
        self.assertEqual({p['imageBase'] for p in headers if p['path'] != 'default.xex'}, {'0x88000000'})

    def test_registry_preserves_bin_names_and_registers_both_isolated_libraries(self):
        records = generate_game.targets(self.manifest, self.game)
        source = generate_game.registry_source(records, 'sdoj')
        self.assertEqual(source.count('RegisterRecompiledModule('), 2)
        for name in ('CA022100', 'CA022110'):
            self.assertIn(f'"{name}.bin","{name}.bin"', source)
            self.assertIn(f'"sdoj_{name}.dll"', source)
            self.assertNotIn(name + '.xex', source)
        self.assertNotIn('PPC_FUNC_IMPL', source)

    def test_incomplete_resource_program_or_redirected_module_is_rejected(self):
        for filename in ('CA022100.xex', 'elsewhere/CA022100.bin', 'PKG000.bin', '../CA022100.bin'):
            bad = copy.deepcopy(self.manifest)
            bad['modules'][0]['guest_path'] = filename
            with self.subTest(filename=filename), self.assertRaises(ValueError):
                generate_game.targets(bad, self.game)
        bad = copy.deepcopy(self.manifest)
        bad['modules'].pop()
        with self.assertRaises(ValueError):
            generate_game.targets(bad, self.game)

    def test_build_selection_is_shared_not_another_sdk(self):
        self.assertEqual(selected_games('sdoj'), ('sdoj',))
        self.assertEqual(selected_games('sdoj,futari,espgaluda2'), ('espgaluda2', 'futari', 'sdoj'))

    def test_base_generation_rejects_automatic_patch_companions(self):
        names = ['default.xexp', 'CA022100.binp', 'CA022110.binp']
        self.assertEqual(self.game['forbiddenFiles'], names)
        with tempfile.TemporaryDirectory() as directory:
            data = Path(directory)
            fixture = dict(files=[], forbiddenFiles=names)
            self.assertEqual(generate_game.verify_data(fixture, data), [])
            for name in names:
                path = data / name
                path.write_bytes(b'authored, not a game patch')
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Unsupported companion'):
                    generate_game.verify_data(fixture, data)
                self.assertEqual(path.read_bytes(), b'authored, not a game patch')
                path.unlink()

    def test_texture_compatibility_is_title_local_before_runtime_setup(self):
        shared = (ROOT / 'backends/xbox360/host.cpp').read_text(encoding='utf-8')
        adapter = (ROOT / 'games/sdoj/host.cpp').read_text(encoding='utf-8')
        hook = shared.split('void CollectionApp::OnPreSetup(', 1)[1].split('\n}', 1)[0]
        self.assertIn('OnGamePreSetup(config);', hook)
        self.assertNotIn('gpu_allow_invalid_fetch_constants', shared)
        self.assertIn('OnGamePreSetup(rex::RuntimeConfig&) override', adapter)
        self.assertIn('SetFlagByName("gpu_allow_invalid_fetch_constants","true")', adapter)
        for game in ('espgaluda2', 'futari'):
            self.assertNotIn('gpu_allow_invalid_fetch_constants',
                             (ROOT / f'games/{game}/host.cpp').read_text(encoding='utf-8'))

    def test_both_module_end_callbacks_have_independent_declarations(self):
        for module, address in (('CA022100', '0x880943E8'), ('CA022110', '0x880952F0')):
            filename = module + '-functions.toml'
            manifest = next(item for item in self.manifest['modules'] if item['guest_path'] == module + '.bin')
            self.assertEqual(manifest['includes'], [filename, module + '-input-diagnostics.toml', module + '-audio-diagnostics.toml'])
            callbacks = tomllib.loads((ROOT / 'games/sdoj/recomp' / filename).read_text(encoding='utf-8'))['functions']
            self.assertEqual(set(callbacks), {address})
            self.assertEqual(callbacks[address]['size'], 4)

    def test_reviewed_resident_callbacks_exclude_frame_dependent_tails(self):
        functions = tomllib.loads((ROOT / 'games/sdoj/recomp/default-functions.toml').read_text(encoding='utf-8'))['functions']
        self.assertEqual(len(functions), 63)
        self.assertEqual(functions['0x825B6F48']['size'], 12)
        self.assertEqual(functions['0x823D62C0']['size'], 16)
        self.assertEqual(functions['0x824982C0']['size'], 136)
        self.assertEqual(functions['0x82251C10']['size'], 244)
        self.assertEqual(functions['0x8217F850']['size'], 208)
        self.assertEqual(functions['0x82253058']['size'], 252)
        self.assertTrue(all(4 <= item['size'] <= 252 and item['size'] % 4 == 0 for item in functions.values()))
        self.assertFalse({'0x822478B0', '0x822643D0', '0x8246932C'} & functions.keys())
        self.assertIn('default-functions.toml', self.manifest['entrypoint']['includes'])

    def test_native_network_wrappers_have_title_local_abi_hooks(self):
        filename = 'community-overrides.toml'
        self.assertIn(filename, self.manifest['entrypoint']['includes'])
        hooks = tomllib.loads((ROOT / 'games/sdoj/recomp' / filename).read_text(encoding='utf-8'))['midasm_hook']
        expected = {
            0x8221C418: ('SdojCommunityEstimateRank', 5),
            0x8221C188: ('SdojStorageUpload', 5),
            0x8221BEA8: ('SdojStorageDownload', 7),
            0x8221C5F8: ('SdojStorageLocalPath', 7),
            0x8221C690: ('SdojStorageRemotePath', 7),
        }
        api_hooks = [hook for hook in hooks if hook.get('return')]
        self.assertEqual(len(api_hooks), len(expected))
        source = (ROOT / 'games/sdoj/community/guest_hooks.cpp').read_text(encoding='utf-8')
        for hook in api_hooks:
            name, count = expected[hook['address']]
            self.assertEqual(hook['name'], name)
            self.assertEqual(hook['registers'], [f'r{i}' for i in range(3, 3 + count)])
            self.assertTrue(hook['return'])
            self.assertIn('void ' + name + '(', source)
        self.assertNotIn('G2', source)

    def test_optional_native_trace_observes_not_replaces_game_functions(self):
        hooks = tomllib.loads((ROOT / 'games/sdoj/recomp/community-overrides.toml').read_text(encoding='utf-8'))['midasm_hook']
        traces = [hook for hook in hooks if hook['name'].startswith('SdojTrace')]
        self.assertEqual({hook['address'] for hook in traces},
                         {0x82155AA8, 0x82155C30, 0x82155C48, 0x82155C60, 0x82155CA8, 0x82155CF0})
        self.assertTrue(all(not hook.get('return', False) for hook in traces))
        source = (ROOT / 'games/sdoj/community/guest_hooks.cpp').read_text(encoding='utf-8')
        self.assertIn('REXCVAR_DEFINE_BOOL(sdoj_native_trace,false,', source)
        self.assertIn('QueryRangeAccess', source)

    def test_disc_input_observers_use_reviewed_sites_without_replacing_calls(self):
        filename = 'input-diagnostics-overrides.toml'
        self.assertIn(filename, self.manifest['entrypoint']['includes'])
        hooks = tomllib.loads((ROOT / 'games/sdoj/recomp' / filename).read_text(encoding='utf-8'))['midasm_hook']
        expected = {
            0x82178B78: ('SdojInputPassBegin', ['r3']),
            0x821A3D90: ('SdojInputPollExisting', ['r3', 'r30', 'r27']),
            0x821A3EA8: ('SdojInputPollDiscovery', ['r3', 'r28', 'r30']),
            0x821A3C38: ('SdojInputButtonCopy', ['r11', 'r31']),
            0x82178BFC: ('SdojInputPassEnd', ['r31']),
        }
        self.assertEqual(len(hooks), len(expected))
        for hook in hooks:
            self.assertEqual((hook['name'], hook['registers']), expected[hook['address']])
            self.assertFalse(hook.get('return', False))
        self.assertNotIn(0x822169E0, expected)  # Different TU input wrapper.

    def test_disc_input_trace_is_opt_in_bounded_and_read_only(self):
        source = (ROOT / 'games/sdoj/diagnostics/input_trace.cpp').read_text(encoding='utf-8')
        self.assertIn('REXCVAR_DEFINE_BOOL(sdoj_input_trace, false,', source)
        self.assertIn('constexpr unsigned MaxRecords = 2048;', source)
        self.assertIn('compare_exchange_weak', source)
        self.assertIn('QueryRangeAccess', source)
        self.assertIn('not_gameplay_or_scanout_latency', source)
        for forbidden in ('REX_STORE_', 'StartCommunity', 'XamInputGetState(', 'sub_821', 'SetFlagByName'):
            self.assertNotIn(forbidden, source)
        cmake = (ROOT / 'games/sdoj/CMakeLists.txt').read_text(encoding='utf-8')
        self.assertLess(cmake.index('add_test(NAME sdoj_input_trace'), cmake.index('if(CAVE_BUILD_GAME)'))

    def test_core_input_observers_are_separate_and_non_replacing(self):
        for module, expected in {
            'CA022100': {0x88051364: ('SdojCoreNormalBuilt', []), 0x88051D74: ('SdojCoreNormalCopy', ['r27'])},
            'CA022110': {0x880512DC: ('SdojCoreArrangeBuilt', []), 0x88051CFC: ('SdojCoreArrangeCopy', ['r25'])},
        }.items():
            prefix = 'SdojRenderNormal' if module == 'CA022100' else 'SdojRenderArrange'
            expected.update({address: (prefix + 'Swap', []) for address in
                             (0x880330B4, 0x880331B4, 0x880331EC)})
            expected.update({0x880331F0: (prefix + 'WaitBegin', ['r3']),
                             0x88033304: (prefix + 'WaitEnd', [])})
            hooks = tomllib.loads((ROOT / f'games/sdoj/recomp/{module}-input-diagnostics.toml').read_text(encoding='utf-8'))['midasm_hook']
            self.assertEqual(len(hooks), 7)
            for hook in hooks:
                self.assertEqual((hook['name'], hook['registers']), expected[hook['address']])
                self.assertFalse(hook.get('return', False))
        host = (ROOT / 'games/sdoj/diagnostics/input_trace.cpp').read_text(encoding='utf-8')
        bridge = (ROOT / 'games/sdoj/diagnostics/input_module_trace.cpp').read_text(encoding='utf-8')
        self.assertIn('std::try_to_lock', host)
        self.assertIn('gate.unlock();', host)
        self.assertIn('GetProcAddress', bridge)
        self.assertIn('extern "C" __declspec(dllexport) void SdojCoreInputEvent', host)
        self.assertNotIn('REX_STORE_', bridge)

    def test_render_trace_is_bounded_read_only_and_not_scanout_measurement(self):
        source = (ROOT / 'games/sdoj/diagnostics/render_trace.cpp').read_text(encoding='utf-8')
        self.assertIn('REXCVAR_DEFINE_BOOL(sdoj_render_trace, false,', source)
        self.assertIn('constexpr unsigned MaxRecords = 2048;', source)
        self.assertIn('std::try_to_lock', source)
        self.assertIn('gate.unlock();', source)
        self.assertIn('last_copy_not_buffer_lineage not_scanout_latency', source)
        for forbidden in ('REX_STORE_', 'XamInputGetState(', 'SetFlagByName', 'sub_880'):
            self.assertNotIn(forbidden, source)

    def test_audio_observers_keep_distinct_queues_and_original_calls(self):
        for module, prefix, sites in (
            ('CA022100','SdojAudioNormal',{0x8806E960,0x8806E9F4,0x8806EA84,0x8806EB24,0x88070CD8,0x88070DB0}),
            ('CA022110','SdojAudioArrange',{0x8806F348,0x8806F3DC,0x8806F46C,0x8806F50C,0x88071588,0x88071660}),
        ):
            hooks=tomllib.loads((ROOT / f'games/sdoj/recomp/{module}-audio-diagnostics.toml').read_text(encoding='utf-8'))['midasm_hook']
            self.assertEqual(len(hooks),14)
            self.assertEqual({h['address'] for h in hooks if h['address']>=0x88060000},sites)
            self.assertTrue(all(h['name'].startswith(prefix) and not h.get('return',False) for h in hooks))
            dispatch=[h for h in hooks if h['name'].endswith('Dispatch')]
            self.assertEqual(len(dispatch),4)
            self.assertTrue(all(h['registers'][:5]==['r29','r4','r28','r5','f1'] for h in dispatch))
            self.assertEqual({h['address'] for h in hooks if h['name'].endswith('Returned')},
                             {0x8803377C, 0x88033910})
        host=(ROOT/'games/sdoj/diagnostics/audio_trace.cpp').read_text(encoding='utf-8')
        bridge=(ROOT/'games/sdoj/diagnostics/audio_module_trace.cpp').read_text(encoding='utf-8')
        self.assertIn('REXCVAR_DEFINE_BOOL(sdoj_audio_trace, false,',host)
        self.assertIn('std::try_to_lock',host)
        self.assertIn('constexpr unsigned MaxRecords = 4096;',host)
        self.assertIn('return_is_untyped',host)
        for source in (host,bridge):
            self.assertNotIn('REX_STORE_',source)
            self.assertNotIn('SetThreadPriority',source)

    def test_audio_engine_observers_bracket_original_push_and_play_joins(self):
        filename='audio-diagnostics-overrides.toml'
        self.assertIn(filename,self.manifest['entrypoint']['includes'])
        hooks=tomllib.loads((ROOT/'games/sdoj/recomp'/filename).read_text(encoding='utf-8'))['midasm_hook']
        self.assertEqual({h['address'] for h in hooks}, {
            0x8217D4A8,0x8217D514,0x821900B8,0x82190160,
            0x821906FC,0x82190774,0x821908C4,0x8219093C,0x82190778,0x82190940})
        self.assertEqual({h['address'] for h in hooks if h['name'].endswith('PlayReturned')},
                         {0x82190778,0x82190940})
        self.assertTrue(all(not h.get('return',False) for h in hooks))
        self.assertTrue(all(h['registers']==['r31','f1','f27','f30','f31','r11']
                            for h in hooks if h['name'].endswith('Play')))


if __name__ == '__main__':
    unittest.main()
