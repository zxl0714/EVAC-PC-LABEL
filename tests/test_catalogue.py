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
        self.assertEqual([g['id'] for g in games], ['futari', 'espgaluda2', 'ketsui', 'sdoj', 'daioujou', 'esprade', 'guwange', 'feveron', 'garegga', 'espgaluda', 'ibara'])
        self.assertFalse(any(g['comingSoon'] for g in games))
        self.assertTrue(all(g['integrated'] for g in games))
        feveron=next(g for g in games if g['id']=='feveron')
        self.assertTrue(feveron['communitySupported'])
        self.assertEqual(feveron['runtimeFiles'], ['runtime/ps4/feveron.exe', 'runtime/ps4/KetsuiGpuRuntime.dll',
                                                'runtime/games/feveron/runtime-manifest.ini'])
        self.assertEqual(feveron['dataManifest'], 'feveron/data.json')

    def test_garegga_korean_base_keeps_all_original_core_revisions(self):
        game=next(g for g in catalogue.load(self.path) if g['id']=='garegga')
        manifest=catalogue.read_json(self.root/'games/garegga/data.json')
        self.assertEqual((manifest['titleId'],manifest['version'],manifest['layout']),('CUSA10207','01.00','base'))
        self.assertEqual(len(game['files']),46)
        programs={f['path'] for f in game['files'] if f['program'] and '/' not in f['path']}
        self.assertEqual(programs,{'eboot.bin','emu_integration.prx','emu_integration_01100.prx',
            *('garegga_'+version+'.prx' for version in ('01000','01010','01020','01030','01040'))})
        self.assertEqual(set(game['forbiddenFiles']),{'ps4data_patch_info.psb.m','ps4data_patch_body.bin'})

    def test_feveron_base_manifest_uses_application_version_and_own_modules(self):
        game = next(g for g in catalogue.load(self.path) if g['id'] == 'feveron')
        manifest = catalogue.read_json(self.root / 'games/feveron/data.json')
        self.assertEqual((manifest['titleId'], manifest['version'], manifest['layout']),
                         ('CUSA06735', '01.00', 'base'))
        self.assertEqual(len(game['files']), 44)
        self.assertEqual([(v['id'], v['version'], v['layout']) for v in game['variants']],
                         [('base-us', '01.00', 'base')])
        programs = {f['path'] for f in game['files'] if f['program'] and '/' not in f['path']}
        self.assertEqual(programs, {'eboot.bin', 'emu_integration_01200.prx',
                                    'dfeveron_01000.prx', 'dfeveron_01010.prx'})
        self.assertEqual(set(game['forbiddenFiles']), {'ps4data_patch_info.psb.m', 'ps4data_patch_body.bin'})

    def test_esprade_uses_merged_104_data_and_normal_community_entry(self):
        game = next(g for g in catalogue.load(self.path) if g['id'] == 'esprade')
        manifest = catalogue.read_json(self.root / 'games/esprade/data.json')
        self.assertEqual((game['backend'], manifest['titleId'], manifest['version']), ('ps4', 'CUSA15155', '01.04'))
        self.assertEqual(manifest['layout'], 'merged-patch')
        self.assertEqual([(v['id'], v['version'], v['layout']) for v in game['variants']],
                         [('update-104', '01.04', 'merged-patch'), ('base-self', '01.00', 'base')])
        self.assertEqual(len(game['files']), 47)
        self.assertIn('esprade_01030.prx', game['variants'][1]['forbiddenFiles'])
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
                         [('update-102', '01.02', 'merged-patch', 44), ('base', '01.00', 'base', 38),
                          ('base-self', '01.00', 'base', 38), ('update-103', '01.03', 'merged-patch', 42),
                          ('base-pkg-extracted', '01.00', 'base', 38),
                          ('update-103-pkg-extracted', '01.03', 'merged-patch', 42),
                          ('base-store-dump', '01.00', 'base', 37)])
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
        self.assertIn('std::array<DataVariant,7>', catalogue.generate(catalogue.load(self.path)))

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

    def test_ps4_fingerprints_only_identify_known_programs(self):
        raw = catalogue.read_json(self.root / 'games/daioujou/data.json')
        variants = catalogue.data_variants(raw)
        entry = lambda variant: next(f for f in variant['files'] if f['path'] == 'eboot.bin')
        self.assertEqual(len({entry(v)['programFingerprint'] for v in variants}), 2)
        for value in (None, 'not a SHA-256'):
            modified = copy.deepcopy(raw)
            entry(modified)['programFingerprint'] = value
            with self.assertRaises(ValueError):
                catalogue.data_variants(modified)
        modified = copy.deepcopy(raw)
        entry(modified['alternatives'][0])['programFingerprint'] = entry(modified)['programFingerprint']
        with self.assertRaises(ValueError):
            catalogue.data_variants(modified)
        modified = copy.deepcopy(raw)
        next(f for f in modified['files'] if not f['program'])['programFingerprint'] = 'a'*64
        with self.assertRaises(ValueError):
            catalogue.data_variants(modified)

    def test_every_supported_ps4_program_has_a_wrapper_independent_identity(self):
        games = [g for g in catalogue.load(self.path) if g['backend'] == 'ps4']
        self.assertEqual(len(games), 5)
        for game in games:
            for variant in game['variants']:
                for item in variant['files']:
                    with self.subTest(game=game['id'], variant=variant['id'], path=item['path']):
                        self.assertEqual(bool(item.get('programFingerprint')), item['program'])

    def test_equivalent_ketsui_packages_require_distinct_metadata_and_identical_code(self):
        raw = catalogue.read_json(self.root / 'games/ketsui/data.json')
        variants = catalogue.data_variants(raw)
        entry = lambda v: next(f for f in v['files'] if f['path'] == 'eboot.bin')
        self.assertEqual(entry(variants[1])['programFingerprint'], entry(variants[2])['programFingerprint'])
        for field in ('metadata', 'code', 'version'):
            changed = copy.deepcopy(raw)
            base, alias = changed['alternatives'][:2]
            if field == 'metadata':
                for name in ('sce_sys/param.sfo', 'sce_sys/playgo-chunk.sha', 'sce_sys/about/right.sprx'):
                    next(f for f in alias['files'] if f['path'] == name)['sha256'] = next(f for f in base['files'] if f['path'] == name)['sha256']
            elif field == 'code':
                next(f for f in alias['files'] if f['program'] and f['path'] != 'eboot.bin')['programFingerprint'] = '0'*64
            else:
                alias['version'] = '01.02'
            with self.subTest(field=field), self.assertRaises(ValueError):
                catalogue.data_variants(changed)

    def test_extracted_pkg_variants_keep_canonical_programs_and_game_resources(self):
        for game, updated in (('daioujou', 'update-104'), ('ketsui', 'update-103')):
            raw = catalogue.read_json(self.root / 'games' / game / 'data.json')
            variants = {v['id']: v for v in catalogue.data_variants(raw)}
            for original, extracted in (('base-self', 'base-pkg-extracted'),
                                        (updated, updated + '-pkg-extracted')):
                with self.subTest(game=game, variant=extracted):
                    source = variants[original]
                    alias = variants[extracted]
                    self.assertEqual((alias['version'], alias['layout'], alias['forbiddenFiles']),
                                     (source['version'], source['layout'], source['forbiddenFiles']))
                    metadata = lambda f: f['path'].startswith('sce_sys/') or f['path'].startswith('sce_discmap')
                    self.assertEqual([f for f in source['files'] if not metadata(f)],
                                     [f for f in alias['files'] if not metadata(f)])
                    self.assertNotEqual(
                        [(f['path'], f['sha256']) for f in source['files'] if metadata(f)],
                        [(f['path'], f['sha256']) for f in alias['files'] if metadata(f)])
                    self.assertFalse(
                        {f['path'] for f in alias['files']} &
                        {'sce_sys/.digests', 'sce_sys/.entry_keys', 'sce_sys/.entry_names',
                         'sce_sys/.general_digests', 'sce_sys/.metas', 'sce_sys/target-deltainfo.dat'})
                    for name in ('sce_sys/param.sfo', 'sce_sys/playgo-chunk.sha'):
                        self.assertTrue(any(f['path'] == name for f in alias['files']))

    def test_shared_canonical_roots_require_unambiguous_verified_package_metadata(self):
        original = catalogue.read_json(self.root / 'games/daioujou/data.json')
        for change in ('metadata', 'code', 'root', 'layout'):
            raw = copy.deepcopy(original)
            source = raw
            alias = next(v for v in raw['alternatives'] if v['id'] == 'update-104-pkg-extracted')
            if change == 'metadata':
                for name in ('sce_sys/param.sfo', 'sce_sys/playgo-chunk.sha', 'sce_sys/about/right.sprx'):
                    next(f for f in alias['files'] if f['path'] == name)['sha256'] = next(f for f in source['files'] if f['path'] == name)['sha256']
            elif change == 'code':
                next(f for f in alias['files'] if f['path'] == 'daioujou_01000.prx')['programFingerprint'] = '0' * 64
            elif change == 'root':
                next(f for f in alias['files'] if f['path'] == 'eboot.bin')['programFingerprint'] = '0' * 64
            else:
                alias['layout'] = 'base'
            with self.subTest(change=change), self.assertRaises(ValueError):
                catalogue.data_variants(raw)

    def test_pre_extraction_store_base_variants_preserve_canonical_code_and_resources(self):
        for game, original, count in (('daioujou', 'base-self', 40), ('ketsui', 'base', 37)):
            raw = catalogue.read_json(self.root / 'games' / game / 'data.json')
            variants = {v['id']: v for v in catalogue.data_variants(raw)}
            source, alias = variants[original], variants['base-store-dump']
            with self.subTest(game=game):
                self.assertEqual((alias['version'], alias['layout'], alias['forbiddenFiles']),
                                 (source['version'], source['layout'], source['forbiddenFiles']))
                self.assertEqual(len(alias['files']), count)
                self.assertNotIn('sce_sys/license.dat', {f['path'] for f in alias['files']})
                metadata = lambda f: f['path'].startswith('sce_sys/') or f['path'].startswith('sce_discmap')
                self.assertEqual([f for f in source['files'] if not metadata(f)],
                                 [f for f in alias['files'] if not metadata(f)])
                for name in ('sce_sys/param.sfo', 'sce_sys/playgo-chunk.sha', 'sce_sys/about/right.sprx'):
                    self.assertTrue(any(f['path'] == name for f in alias['files']))
                if game == 'ketsui':
                    for name in ('sce_sys/param.sfo', 'sce_sys/playgo-chunk.sha'):
                        get_hash = lambda v: next(f['sha256'] for f in v['files'] if f['path'] == name)
                        self.assertEqual(get_hash(source), get_hash(alias))
                    get_rights = lambda v: next(f['sha256'] for f in v['files'] if f['path'] == 'sce_sys/about/right.sprx')
                    self.assertNotEqual(get_rights(source), get_rights(alias))

    def test_ps_store_compatible_manifests_do_not_require_system_artwork(self):
        for game in catalogue.load(self.path):
            if game['id'] not in ('daioujou', 'ketsui'):
                continue
            for variant in game['variants']:
                with self.subTest(game=game['id'], variant=variant['id']):
                    paths = {item['path'] for item in variant['files']}
                    self.assertTrue(paths.isdisjoint({'sce_sys/icon0.dds', 'sce_sys/pic0.dds'}))

    def test_daioujou_candidate_uses_own_data_and_shared_ps4_runtime(self):
        game=next(g for g in catalogue.load(self.path) if g['id']=='daioujou')
        manifest=catalogue.read_json(self.root/'games/daioujou/data.json')
        self.assertEqual((game['backend'],manifest['titleId'],manifest['version']),('ps4','CUSA32997','01.04'))
        self.assertEqual(manifest['layout'],'merged-patch')
        self.assertEqual(len(game['files']),54)
        self.assertEqual(sum(f['bytes'] for f in game['files']),1111070033)
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
