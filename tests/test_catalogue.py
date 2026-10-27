import copy
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import generate_catalogue as catalogue
import prepare_tools
import project


class CatalogueTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(__file__).resolve().parents[1]
        self.path = self.root / 'games/catalogue.json'

    def test_real_catalogue_order_and_states(self):
        games = catalogue.load(self.path)
        self.assertEqual([g['id'] for g in games], ['futari', 'espgaluda2', 'ketsui', 'sdoj', 'daioujou', 'esprade', 'guwange', 'feveron'])
        self.assertEqual([g['comingSoon'] for g in games], [False] * 7 + [True])
        self.assertEqual([g['id'] for g in games if g['integrated']], ['futari', 'espgaluda2', 'ketsui', 'sdoj', 'daioujou', 'esprade', 'guwange'])
        feveron=games[-1]
        self.assertFalse(feveron['communitySupported'])
        self.assertEqual(feveron['runtimeFiles'], [])
        self.assertEqual(feveron['dataManifest'], '')

    def test_esprade_uses_merged_104_data_and_normal_community_entry(self):
        game = next(g for g in catalogue.load(self.path) if g['id'] == 'esprade')
        manifest = catalogue.read_json(self.root / 'games/esprade/data.json')
        self.assertEqual((game['backend'], manifest['titleId'], manifest['version']), ('ps4', 'CUSA15155', '01.04'))
        self.assertEqual(manifest['layout'], 'merged-patch')
        self.assertEqual([(v['id'], v['version'], v['layout']) for v in game['variants']],
                         [('update-104', '01.04', 'merged-patch')])
        self.assertEqual(len(game['files']), 48)
        self.assertTrue(game['integrated'] and game['communitySupported'])
        self.assertFalse(game['comingSoon'])
        self.assertEqual(game['runtimeFiles'], ['runtime/ps4/esprade.exe', 'runtime/ps4/KetsuiGpuRuntime.dll',
                                              'runtime/games/esprade/runtime-manifest.ini'])
        self.assertTrue({'eboot.bin', 'emu_integration_01500.prx', 'esprade_01000.prx',
                         'esprade_01010.prx', 'esprade_01020.prx', 'esprade_01030.prx'}.issubset(
                             {f['path'] for f in game['files'] if f['program']}))

    def test_sdoj_uses_verified_disc_data_and_shared_online_launch(self):
        game = next(g for g in catalogue.load(self.path) if g['id'] == 'sdoj')
        self.assertTrue(game['integrated'])
        self.assertTrue(game['communitySupported'])
        self.assertEqual(game['executable'], 'runtime/xbox360/sdoj.exe')
        self.assertEqual(len(game['files']), 114)
        self.assertEqual(sum(item['bytes'] for item in game['files']), 3274143397)
        self.assertEqual({item['path'] for item in game['files'] if item['program']},
                         {'default.xex', 'CA022100.bin', 'CA022110.bin'})
        self.assertFalse(any(item['path'].startswith('$SystemUpdate') for item in game['files']))

    def test_g2_inventory_is_complete_metadata_only(self):
        game = catalogue.load(self.path)[1]
        self.assertEqual(len(game['files']), 155)
        self.assertEqual(sum(f['bytes'] for f in game['files']), 677185607)
        self.assertEqual(sum(f['program'] for f in game['files']), 8)
        self.assertTrue(all(set(f) == {'path', 'bytes', 'sha256', 'program'} for f in game['files']))

    def test_ketsui_base_and_merged_update_have_separate_complete_manifests(self):
        game = next(game for game in catalogue.load(self.path) if game['id'] == 'ketsui')
        self.assertEqual([(v['id'], v['version'], v['layout'], len(v['files'])) for v in game['variants']],
                         [('update-102', '01.02', 'merged-patch', 47), ('base', '01.00', 'base', 41),
                          ('base-self', '01.00', 'base', 41)])
        self.assertEqual(game['files'], game['variants'][0]['files'])
        for variant in game['variants']:
            files = {item['path']: item for item in variant['files']}
            self.assertIn('eboot.bin', files)
            self.assertIn('sce_module/libc.prx', files)
            self.assertFalse(any(name.startswith('CUSA09062-') for name in files))
            self.assertTrue({'CUSA09062-app', 'CUSA09062-patch'}.issubset(variant['forbiddenFiles']))
        base = game['variants'][1]
        self.assertIn('ps4data_patch_body.bin', base['forbiddenFiles'])
        self.assertIn('ps4data_patch_info.psb.m', base['forbiddenFiles'])
        self.assertIn('std::array<DataVariant,3>', catalogue.generate(catalogue.load(self.path)))

    def test_issue_1_plain_self_manifests_keep_exact_versions_and_reject_mixed_updates(self):
        games = {game['id']: game for game in catalogue.load(self.path)}
        identities = {
            'ketsui': '9113b52b4b61c71cf557a74f9d9e3dc29af91f572c245cdec5ba09c7e1b0a8ed',
            'daioujou': '8256abd9bbe18ff68c5196d0104b84b94254fb355fcb0ca3df2c20b03802df79',
        }
        for game, expected in identities.items():
            with self.subTest(game=game):
                variant = next(v for v in games[game]['variants'] if v['id'] == 'base-self')
                self.assertEqual((variant['version'], variant['layout'], variant['entry']), ('01.00', 'base', 'eboot.bin'))
                files = {f['path']: f for f in variant['files']}
                self.assertEqual(files['eboot.bin']['sha256'], expected)
                self.assertIn('ps4data_patch_body.bin', variant['forbiddenFiles'])
                self.assertIn('ps4data_patch_info.psb.m', variant['forbiddenFiles'])
                self.assertFalse(set(files) & set(variant['forbiddenFiles']))
        base = next(v for v in games['daioujou']['variants'] if v['id'] == 'base-self')
        for module in ('daioujou_01010.prx', 'daioujou_01020.prx', 'daioujou_01030.prx'):
            self.assertIn(module, base['forbiddenFiles'])
        self.assertEqual(games['daioujou']['variants'][0]['id'], 'update-104')
        base_ketsui = {f['path']: f for f in games['ketsui']['variants'][1]['files']}
        wrapped = {f['path']: f for f in games['ketsui']['variants'][2]['files']}
        self.assertEqual(base_ketsui['ps4data_body.bin'], wrapped['ps4data_body.bin'])

    def test_invalid_or_ambiguous_data_variants_are_rejected(self):
        original = catalogue.read_json(self.root / 'games/ketsui/data.json')
        cases = []
        for field, value in [('id', original['variantId']), ('entry', 'wrapped/eboot.bin'),
                             ('version', '1.00'), ('layout', 'legacy-pair'), ('forbiddenFiles', ['eboot.bin'])]:
            raw = copy.deepcopy(original); raw['alternatives'][0][field] = value; cases.append(raw)
        raw = copy.deepcopy(original)
        raw['alternatives'][0]['files'][0]['sha256'] = original['files'][0]['sha256']
        # Match the actual entry, not whichever file sorts first.
        next(f for f in raw['alternatives'][0]['files'] if f['path'] == 'eboot.bin')['sha256'] = next(f for f in original['files'] if f['path'] == 'eboot.bin')['sha256']
        cases.append(raw)
        raw = copy.deepcopy(original); raw.pop('variantId'); cases.append(raw)
        for raw in cases:
            with self.subTest(raw=raw.get('variantId')), self.assertRaises(ValueError):
                catalogue.data_variants(raw)

    def test_daioujou_candidate_uses_own_data_and_shared_ps4_runtime(self):
        game=next(g for g in catalogue.load(self.path) if g['id']=='daioujou')
        manifest=catalogue.read_json(self.root/'games/daioujou/data.json')
        self.assertEqual((game['backend'],manifest['titleId'],manifest['version']),('ps4','CUSA32997','01.04'))
        self.assertEqual(manifest['layout'],'merged-patch')
        self.assertEqual(len(game['files']),60)
        self.assertEqual(sum(f['bytes'] for f in game['files']),1116174744)
        self.assertNotIn('Project.gp4',{f['path'] for f in game['files']})
        self.assertFalse(game['comingSoon'])
        self.assertTrue(game['integrated'])
        self.assertTrue(game['communitySupported'])
        self.assertEqual(game['executable'],'runtime/ps4/daioujou.exe')
        self.assertEqual(game['runtimeFiles'],['runtime/ps4/daioujou.exe',
            'runtime/ps4/KetsuiGpuRuntime.dll','runtime/games/daioujou/runtime-manifest.ini'])

    def test_futari_optional_packages_are_independent_metadata(self):
        game = catalogue.load(self.path)[0]
        self.assertEqual(len(game['files']), 52)
        self.assertEqual([p['id'] for p in game['content']], ['black-label', 'ver101'])
        self.assertEqual([len(p['files']) for p in game['content']], [4, 1])
        self.assertEqual(sum(f['bytes'] for p in game['content'] for f in p['files']), 275443232)
        self.assertFalse(any(f['path'].endswith('.header') for p in game['content'] for f in p['files']))
        self.assertFalse(any(p['content'] for p in catalogue.load(self.path)[1:]))

    def test_daioujou_native_boards_do_not_reuse_ketsui_filter_rules(self):
        reference=catalogue.read_json(self.root/'games/daioujou/community-references.json')
        self.assertEqual(reference['status'],'native-https-roundtrip-verified')
        self.assertEqual(reference['score']['nativeUnitInDisplayPoints'],10)
        self.assertEqual([item['base'] for item in reference['boardGroups']],[0,10,20,30,40,50,60])
        self.assertEqual([item['offset'] for item in reference['boardFilters']],list(range(7)))
        self.assertEqual(reference['boardFilters'][-1]['ship'],'all')
        self.assertFalse(reference['boardGroups'][1]['submissionEnabled'])
        self.assertTrue(reference['eligibility']['noContinues'])
        self.assertIn('sceNpScoreRecordGameDataAsync',reference['nativeInterfaces'])
        layout=reference['replayLayout']
        self.assertEqual(layout['recordBytes'],layout['dataOffset']+layout['dataBytes']+layout['trailingBytes'])
        self.assertEqual(layout['recordBytes'],0x65600)
        self.assertEqual(layout['containerVersion'],0x10030)
        self.assertTrue(layout['nativeRecordingVerified'])
        self.assertTrue(layout['coreDataVersionVerified'])
        self.assertTrue(layout['nativePlaybackVerified'])
        self.assertEqual(layout['observedRecording']['region'],'ARCADE_01000')
        self.assertEqual(layout['observedRecording']['nativeScore']*10,layout['observedRecording']['displayScore'])
        network=reference['networkReplayLayout']
        self.assertEqual(network['recordBytes'],0x80000)
        self.assertEqual(network['dataBytes'],0x78000)
        self.assertEqual(network['recordBytes'],network['dataOffset']+network['dataBytes']+network['trailingBytes'])
        self.assertEqual(network['containerVersion'],layout['containerVersion'])
        self.assertEqual(network['coreDataVersion'],0x10000)
        self.assertEqual(network['arcadeNativeModes'],[0,3])
        self.assertTrue(network['nativeRoundtripVerified'])
        self.assertEqual(network['nativeRoundtripScope'],'normal-collection-https-native-arcade')
        self.assertTrue(network['productionHttpsVerified'])
        https=network['observedHttpsRoundtrip']
        self.assertEqual((https['board'],https['nativeScore'],https['displayScore']),(0,104,1040))
        self.assertEqual(https['networkAttachmentBytes'],26625)
        self.assertTrue(https['offlineColdPlaybackVerified'])
        self.assertEqual(len(https['networkAttachmentSha256']),64)
        self.assertEqual(len(https['downloadedLocalRecordSha256']),64)
        roundtrip=network['observedRoundtrip']
        self.assertEqual(roundtrip['nativeScore']*10,roundtrip['displayScore'])
        self.assertEqual(roundtrip['board'],0)
        self.assertTrue(roundtrip['offlineColdPlaybackVerified'])
        self.assertEqual(roundtrip['networkAttachmentBytes'],26345)
        self.assertEqual(len(roundtrip['networkAttachmentSha256']),64)
        self.assertEqual(len(roundtrip['downloadedLocalRecordSha256']),64)

    def test_invalid_optional_content_metadata(self):
        original = catalogue.read_json(self.root / 'games/futari/data.json')
        cases = []
        for name in ('', 'x'*128, 'invalid\nname', 123):
            value = copy.deepcopy(original); value['optionalContent'][0]['nativeName'] = name; cases.append(value)
        value = copy.deepcopy(original); value['optionalContent'][0]['files'][0]['path'] = '../escape'; cases.append(value)
        value = copy.deepcopy(original); value['optionalContent'][0]['files'] = []; cases.append(value)
        value = copy.deepcopy(original); value['optionalContent'][0]['files'][0]['path'] = 'Headers/generated.header'; cases.append(value)
        value = copy.deepcopy(original); value['optionalContent'][0]['files'][0]['program'] = True; cases.append(value)
        value = copy.deepcopy(original); value['optionalContent'][1]['id'] = 'black_label'; cases.append(value)
        value = copy.deepcopy(original); value['optionalContent'][1]['directory'] = value['optionalContent'][0]['directory']; cases.append(value)
        for manifest in cases:
            with self.subTest(manifest=manifest['optionalContent'][0]['id']), tempfile.TemporaryDirectory() as directory:
                target = Path(directory)
                raw = catalogue.read_json(self.path)
                (target / 'catalogue.json').write_text(json.dumps(raw), encoding='utf-8')
                for game in raw['games']:
                    if game['dataManifest']:
                        output = target / game['dataManifest']; output.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(self.path.parent / game['dataManifest'], output)
                (target / 'futari/data.json').write_text(json.dumps(manifest), encoding='utf-8')
                with self.assertRaises(ValueError):
                    catalogue.load(target / 'catalogue.json')

    def test_generator_is_deterministic_and_contains_only_metadata(self):
        games = catalogue.load(self.path)
        first = catalogue.generate(games)
        self.assertEqual(first, catalogue.generate(copy.deepcopy(games)))
        self.assertIn('std::array<DataFile,155>', first)
        self.assertNotIn('PPCContext', first)

    def test_forbidden_companion_paths_are_unique_and_not_required(self):
        original = catalogue.read_json(self.root / 'games/sdoj/data.json')
        for names in ('default.xexp', ['../escape'], ['default.xexp', 'DEFAULT.XEXP'], ['default.xex']):
            with self.subTest(names=names), tempfile.TemporaryDirectory() as directory:
                target = Path(directory)
                shutil.copytree(self.root / 'games', target / 'games')
                manifest = copy.deepcopy(original)
                manifest['forbiddenFiles'] = names
                (target / 'games/sdoj/data.json').write_text(json.dumps(manifest), encoding='utf-8')
                with self.assertRaises(ValueError):
                    catalogue.load(target / 'games/catalogue.json')

    def test_invalid_relative_paths(self):
        for path in ('../escape', '/absolute', 'C:/drive', 'a\\b', 'a//b', 'a/./b', 'bad\0path', 'file.', 'bad?path'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                catalogue.relative(path)

    def test_duplicate_ids_and_transformed_identifiers(self):
        raw = catalogue.read_json(self.path)
        for other in ('futari', 'fu-tari'):
            value = copy.deepcopy(raw)
            value['games'][1]['id'] = other
            value['games'][1]['dataManifest'] = None
            if other == 'fu-tari':
                value['games'][0]['id'] = 'fu_tari'
            self.reject(value)

    def reject(self, value):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'catalogue.json'
            path.write_text(json.dumps(value), encoding='utf-8')
            # The temporary catalogue needs the same metadata dependencies;
            # missing fixtures must not mask the identity validation under test.
            for game in catalogue.read_json(self.path)['games']:
                if game['dataManifest']:
                    target = path.parent / game['dataManifest']
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(self.path.parent / game['dataManifest'], target)
            with self.assertRaises((ValueError, KeyError)):
                catalogue.load(path)

    def test_unknown_contract_and_invalid_availability(self):
        raw = catalogue.read_json(self.path)
        for version in (2, True):
            value = copy.deepcopy(raw); value['format'] = version
            self.reject(value)
        value = copy.deepcopy(raw); value['games'][0]['integrated'] = True
        value['games'][0]['runtimeFiles'] = []
        self.reject(value)

    def test_duplicate_json_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bad.json'
            path.write_text('{"format":1,"format":2}', encoding='utf-8')
            with self.assertRaises(ValueError):
                catalogue.read_json(path)


class BoundaryTests(unittest.TestCase):
    def test_tool_archive_path_safety(self):
        for path in ('../outside', '/absolute', 'C:/drive', 'folder\\escape'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                prepare_tools.validate_archive_member(path)

    def test_source_audit_current_tree(self):
        result = project.audit_source()
        self.assertFalse(result['gameDataIncluded'])
        self.assertGreater(result['files'], 15)


if __name__ == '__main__':
    unittest.main()
