import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import release_package as release


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cave-authored-package-')
        self.root = Path(self.temporary.name)
        self.games = {key: dict(runtimeFiles=['runtime/fake.dll']) for key in release.V03_GAMES}

    def tearDown(self):
        self.temporary.cleanup()

    def test_player_extraction_helper_is_explicitly_scoped(self):
        self.assertEqual(release.extraction_sources('0.6'), {})
        self.assertEqual(release.extraction_sources('0.4'), {})
        helper = 'tools/Extract-Ps4Pkg.ps1'
        self.assertEqual(set(release.extraction_sources('0.6.1')), {helper})
        record = dict(version='0.6.1', flavor='Public')
        item = dict(path=helper, role='documentation')
        release.validate_windows_item(item, record, self.games, release.V03_GAMES, {}, set())
        for bad in ('tools/unknown.ps1', 'tools/Extract-Ps4Pkg.py'):
            with self.subTest(path=bad), self.assertRaises(ValueError):
                release.validate_windows_item(dict(item, path=bad), record, self.games,
                                              release.V03_GAMES, {}, set())
        with self.assertRaises(ValueError):
            release.validate_windows_item(dict(item, role='runtime'), record, self.games,
                                          release.V03_GAMES, {}, set())

    def test_v061_player_package_contains_extract_helper(self):
        def check(root, games, commit, inspect):
            result = release.build('Public', '0.6.1', root/'releases/public')
            output = self.root/'helper-extracted'
            with zipfile.ZipFile(result['archive']) as zipped:
                zipped.extractall(output)
            record = release.verify_tree(output, games)
            self.assertEqual((output/'tools/Extract-Ps4Pkg.ps1').read_text(), '# authored helper\n')
            self.assertEqual(next(x['role'] for x in record['files']
                                  if x['path'] == 'tools/Extract-Ps4Pkg.ps1'), 'documentation')
        self.check_build_flavors('0.6.1', check)

    def test_v076_package_contains_gui_and_all_dependencies(self):
        def check(root, games, commit, inspect):
            result = release.build('Public', '0.7.6', root/'releases/extractor')
            directory = Path(result['directory'])
            record = release.verify_tree(directory, games)
            expected = {'tools/Extract-PS4.exe', *release.extractor_sources('0.7.6')}
            self.assertTrue(expected.issubset({item['path'] for item in record['files']}))
            self.assertNotIn('tools/Extract.exe', {item['path'] for item in record['files']})
            with zipfile.ZipFile(result['archive']) as zipped:
                self.assertTrue(expected.issubset(set(zipped.namelist())))
            name = 'tools/pkgtool/LibOrbisPkg.dll'
            content = (directory/name).read_bytes()
            (directory/name).unlink()
            missing = dict(record, files=[item for item in record['files'] if item['path'] != name])
            self.rewrite_inventory(directory, missing)
            with self.assertRaisesRegex(ValueError, 'extraction dependencies'):
                release.verify_tree(directory, games)
            (directory/name).write_bytes(content)
            self.rewrite_inventory(directory, record)
            self.assertEqual(release.verify_tree(directory, games)['version'], '0.7.6')
        self.check_build_flavors('0.7.6', check)

    def test_v080_includes_native_xbla_extractor(self):
        self.assertNotIn('tools/Extract-XBLA.exe', release.runtime_names(self.games, version='0.7.6', selection=release.V03_GAMES))
        def check(root, games, commit, inspect):
            result = release.build('Public', '0.8.0', root/'releases/xbla-extractor')
            record = release.verify_tree(Path(result['directory']), games)
            self.assertIn('tools/Extract-XBLA.exe', {item['path'] for item in record['files']})
            with zipfile.ZipFile(result['archive']) as zipped:
                self.assertIn('tools/Extract-XBLA.exe', zipped.namelist())
        self.check_build_flavors('0.8.0', check)

    def test_internal_data_uses_the_verified_variant_without_default_fallback(self):
        primary, base = [{'path': 'updated-fixture'}], [{'path': 'base-fixture'}]
        game = dict(files=primary, variants=[dict(id='update-102', files=primary), dict(id='base', files=base)])
        self.assertIs(release.selected_data_files(game, 'base'), base)
        self.assertIs(release.selected_data_files(game, 'update-102'), primary)
        for invalid in (None, '', 'unknown'):
            with self.subTest(variant=invalid), self.assertRaises(ValueError):
                release.selected_data_files(game, invalid)
        self.assertIs(release.selected_data_files(dict(files=base)), base)
        with self.assertRaises(ValueError):
            release.selected_data_files(dict(files=base), 'base')

    def test_tree_checks_root_ancestors_once_and_returns_all_nested_files(self):
        directory = self.root / '普通目录 with spaces'
        expected = []
        for name in ('a.txt', 'nested/b.txt', 'nested/deeper/c.txt'):
            file = directory / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b'authored tree fixture')
            expected.append(file)
        with patch.object(release, 'reject_link', wraps=release.reject_link) as checked:
            self.assertEqual(release.tree_files(directory), sorted(expected))
        checked.assert_called_once_with(directory)

    def tree_link(self, path, target):
        if os.name == 'nt':
            quote = lambda value: "'" + str(value).replace("'", "''") + "'"
            command = "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path " + quote(path) + ' -Target ' + quote(target) + ' | Out-Null'
            subprocess.run(['powershell', '-NoProfile', '-Command', command], check=True, capture_output=True)
        else:
            path.symlink_to(target, target_is_directory=True)

    def test_tree_rejects_linked_root_parent_and_nested_directory(self):
        target = self.root / 'outside'
        (target / 'nested').mkdir(parents=True)
        sentinel = target / 'nested/unchanged.txt'
        sentinel.write_bytes(b'authored sentinel')
        for location in ('root', 'parent', 'nested'):
            with self.subTest(location=location):
                alias = self.root / ('alias-' + location)
                if location == 'nested':
                    directory = self.root / 'source'
                    directory.mkdir()
                    alias = directory / 'linked-child'
                else:
                    directory = alias / 'nested' if location == 'parent' else alias
                self.tree_link(alias, target)
                try:
                    with self.assertRaisesRegex(ValueError, 'Linked source/output'):
                        release.tree_files(directory)
                    self.assertEqual(sentinel.read_bytes(), b'authored sentinel')
                finally:
                    alias.rmdir() if os.name == 'nt' else alias.unlink()

    def test_tree_rejects_file_symlink_without_following_it(self):
        directory = self.root / 'file-links'
        directory.mkdir()
        target = self.root / 'sentinel.txt'
        target.write_bytes(b'authored sentinel')
        link = directory / 'linked-file'
        try:
            link.symlink_to(target)
        except OSError as error:
            if os.name == 'nt' and getattr(error, 'winerror', None) == 1314:
                self.skipTest('Windows file symlink privilege is unavailable; real junction cases remain covered')
            raise
        try:
            with self.assertRaisesRegex(ValueError, 'Linked source/output'):
                release.tree_files(directory)
            self.assertEqual(target.read_bytes(), b'authored sentinel')
        finally:
            link.unlink()

    def package(self, version='0.2-test', flavor='Public'):
        directory = self.root / '发行 中文 space'
        directory.mkdir()
        entries = []
        selection = release.release_games(version)
        config = release.config_name(version)
        optional_config = set() if flavor == 'Update' else {config}
        for name in release.runtime_names(self.games, selection=selection, version=version, flavor=flavor) | optional_config | {'README.txt'}:
            release.text_new(directory / name, 'authored fixture\n', 'config' if name == config else 'runtime', entries)
        if flavor != 'Update':
            for name, source in release.placement_sources(version).items():
                release.copy_checked(source, directory / name, 'documentation', entries)
        payload = [dict(item, path=item['path'].relative_to(directory).as_posix()) for item in entries]
        record = dict(format=1, version=version, flavor=flavor, games=list(selection), files=payload,
                      placementGuideName=release.PLACEMENT_NAME)
        if flavor == 'Update':
            record.update(baseVersion='0.3', preservedPaths=list(release.UPDATE_PRESERVED))
        (directory / 'release.json').write_text(json.dumps(record), encoding='utf-8')
        sums = ''.join(item['sha256'] + '  ' + item['path'] + '\n' for item in sorted(payload, key=lambda item:item['path']))
        sums += release.digest(directory / 'release.json') + '  release.json\n'
        (directory / 'SHA256SUMS.txt').write_text(sums, encoding='utf-8')
        return directory

    def test_public_tiny_package_hashes_zip_and_unicode(self):
        directory = self.package()
        self.assertEqual(release.verify_tree(directory, self.games)['flavor'], 'Public')
        archive = self.root / 'public.zip'
        release.archive(directory, archive)
        with zipfile.ZipFile(archive) as zipped:
            self.assertIsNone(zipped.testzip())
            self.assertIn(directory.name + '/config.ini', zipped.namelist())
        self.assertTrue(archive.with_suffix('.zip.sha256').read_text().startswith(release.digest(archive)))

    def test_update_extracts_directly_over_v03_and_preserves_player_files(self):
        directory = self.package('0.3.1', 'Update')
        self.assertEqual(release.verify_tree(directory, self.games)['baseVersion'], '0.3')
        archive = self.root / 'update.zip'
        release.archive(directory, archive, include_root=False)
        installed = self.root / 'v0.3 已安装'
        sentinels = {name: ('existing ' + name).encode() for name in (
            'config.ini', 'user-data/config.ini', 'user-data/sdoj/save.bin',
            'user-data/futari/REPLAY.DAT', 'game-data/sdoj/default.xex',
            'prepared/ketsui/eboot.ktprobe', 'cache/sdoj/cache.bin',
            'logs/old/game.log', 'source/README.md')}
        for name, data in sentinels.items():
            file = installed / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(data)
        (installed / 'CAVE-PC-Collection.exe').write_bytes(b'old launcher')
        with zipfile.ZipFile(archive) as zipped:
            self.assertIn('CAVE-PC-Collection.exe', zipped.namelist())
            self.assertIn('release.json', zipped.namelist())
            self.assertNotIn('config.ini', zipped.namelist())
            self.assertNotIn('user-data/config.ini', zipped.namelist())
            zipped.extractall(installed)
        for name, data in sentinels.items():
            self.assertEqual((installed / name).read_bytes(), data, name)
        self.assertEqual((installed / 'CAVE-PC-Collection.exe').read_bytes(), b'authored fixture\n')

    def test_update_rejects_player_files_and_invalid_baseline(self):
        for name in ('config.ini', 'user-data/config.ini', 'game-data/sdoj/default.xex',
                     'prepared/sdoj/image.bin', 'cache/test.bin', 'logs/test.txt',
                     'source/README.md', 'runtime/fake.pdb'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                release.validate_update_name(name, self.games)
        directory = self.package('0.3.1', 'Update')
        path = directory / 'release.json'
        record = json.loads(path.read_text(encoding='utf-8'))
        record['baseVersion'] = '0.2'
        path.write_text(json.dumps(record), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'baseline'):
            release.verify_tree(directory, self.games)
        with self.assertRaisesRegex(ValueError, 'baseline'):
            release.version_name('0.2', 'Update')
        with self.assertRaisesRegex(ValueError, 'does not accept'):
            release.build('Update', '0.3.1', data={'sdoj': self.root}, package_format=1)

    def test_v032_is_a_cumulative_five_game_v03_update(self):
        self.assertEqual(release.version_name('0.3.2','Update'),'CAVE-PC-Collection-v0.3.2-update')
        self.assertEqual(release.release_games('0.3.2'),release.V03_GAMES)
        self.assertEqual(release.config_name('0.3.2'),'user-data/config.ini')
        directory=self.package('0.3.2','Update')
        record=release.verify_tree(directory,self.games)
        self.assertEqual(record['baseVersion'],'0.3')
        self.assertEqual(record['preservedPaths'],list(release.UPDATE_PRESERVED))

    def test_v04_default_config_is_the_only_shipped_user_data(self):
        directory = self.package('0.4-dev')
        self.assertEqual(release.verify_tree(directory, self.games)['flavor'], 'Public')
        self.assertTrue((directory / 'user-data/config.ini').is_file())
        self.assertFalse((directory / 'config.ini').exists())
        for name in ('user-data/futari/savegame.bin', 'user-data/config.ini.pending', 'user-data/other.ini'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                release.validate_public_name(name, self.games, release.V03_GAMES)

    def test_v04_renamed_update_keeps_old_shortcuts_without_a_second_implementation(self):
        self.assertEqual(release.version_name('0.4', 'Update'), 'EVAC-PC-LABEL-v0.4-update')
        directory = self.package('0.4', 'Update')
        record = release.verify_tree(directory, self.games)
        self.assertEqual(record['baseVersion'], '0.3')
        archive = self.root / 'renamed-update.zip'
        release.archive(directory, archive, include_root=False)
        installed = self.root / '已有安装 空格'
        saved = {'user-data/config.ini': b'explicit settings', 'game-data/ketsui/eboot.bin': b'authored data',
                 'user-data/ketsui/replay.bin': b'authored replay', 'source/README.md': b'old source'}
        for name, data in {**saved, release.LEGACY_LAUNCHER: b'old launcher'}.items():
            path = installed / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        with zipfile.ZipFile(archive) as zipped:
            self.assertIn(release.LAUNCHER, zipped.namelist())
            self.assertIn(release.LEGACY_LAUNCHER, zipped.namelist())
            zipped.extractall(installed)
        self.assertEqual((installed / release.LAUNCHER).read_bytes(), (installed / release.LEGACY_LAUNCHER).read_bytes())
        self.assertEqual((installed / release.LEGACY_LAUNCHER).read_bytes(), b'authored fixture\n')
        for name, data in saved.items():self.assertEqual((installed / name).read_bytes(), data)
        with self.assertRaises(ValueError):
            release.validate_public_name(release.LEGACY_LAUNCHER, self.games, release.V03_GAMES, '0.4')

    def test_v04_update_rejects_a_different_old_shortcut_binary_even_with_rewritten_inventory(self):
        directory = self.package('0.4', 'Update')
        alias = directory / release.LEGACY_LAUNCHER; alias.write_bytes(b'not the new native launcher')
        path = directory / 'release.json'; record = json.loads(path.read_text(encoding='utf-8'))
        for item in record['files']:
            if item['path'] == release.LEGACY_LAUNCHER:
                item.update(bytes=alias.stat().st_size, sha256=release.digest(alias))
        path.write_text(json.dumps(record), encoding='utf-8')
        sums = ''.join(item['sha256']+'  '+item['path']+'\n' for item in sorted(record['files'], key=lambda item:item['path']))
        (directory/'SHA256SUMS.txt').write_text(sums+release.digest(path)+'  release.json\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'identical'):release.verify_tree(directory, self.games)

    def test_v04_public_templates_are_exact_guides_not_a_game_data_allowlist(self):
        directory=self.package('0.4-dev')
        guides=release.placement_sources('0.4-dev')
        self.assertEqual(len(guides),6)
        self.assertEqual({file.relative_to(directory).as_posix() for file in (directory/'game-data').rglob('*') if file.is_file()},set(guides))
        for name in ('game-data/ketsui/eboot.bin','game-data/futari/notes.txt','game-data/unknown/README.txt'):
            with self.subTest(name=name),self.assertRaises(ValueError):
                release.validate_public_name(name,self.games,release.V03_GAMES,'0.4-dev')
        for name in guides:
            with self.assertRaises(ValueError):release.validate_update_name(name,self.games,release.V03_GAMES,'0.4')
        guide=directory/next(iter(guides))
        guide.write_bytes(b'authored not-the-shipped-guide')
        # Even a self-consistent repacked inventory cannot replace the guide
        # allowlist with arbitrary bytes disguised as documentation.
        path=directory/'release.json';record=json.loads(path.read_text(encoding='utf-8'))
        for item in record['files']:
            if item['path']==guide.relative_to(directory).as_posix():
                item.update(bytes=guide.stat().st_size,sha256=release.digest(guide))
        path.write_text(json.dumps(record),encoding='utf-8')
        sums=''.join(item['sha256']+'  '+item['path']+'\n' for item in sorted(record['files'],key=lambda item:item['path']))
        (directory/'SHA256SUMS.txt').write_text(sums+release.digest(path)+'  release.json\n',encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'placement guide'):release.verify_tree(directory,self.games)

    def test_release_profiles_preserve_v01_v02_and_require_daioujou_for_v03(self):
        self.assertEqual(release.release_games('0.1'), release.LEGACY_GAMES)
        self.assertEqual(release.release_games('0.2-rc1'), release.GAMES)
        self.assertIn('sdoj', release.release_games('0.2'))
        self.assertEqual(release.release_games('0.3-rc1'), release.V03_GAMES)
        self.assertEqual(release.release_games('0.3'), (*release.GAMES, 'daioujou'))
        self.assertEqual(release.release_games('0.4'), release.V03_GAMES)
        self.assertEqual(release.config_name('0.3'), 'config.ini')
        self.assertEqual(release.config_name('0.4-dev'), 'user-data/config.ini')
        for version in ('', '0.5', '1.0', '../bad'):
            with self.subTest(version=version), self.assertRaises(ValueError):
                release.release_games(version)
        self.assertEqual(release.verify_tree(self.package('0.1-test'), self.games)['games'],
                         list(release.LEGACY_GAMES))

    def test_unaccepted_daioujou_blocks_both_v03_flavors_before_output(self):
        games = [dict(id=key, integrated=True, comingSoon=False, communitySupported=True,
                      runtimeFiles=['runtime/' + key + '.exe']) for key in release.V03_GAMES]
        for field, value in (('integrated', False), ('comingSoon', True),
                             ('communitySupported', False), ('runtimeFiles', [])):
            with self.subTest(field=field):
                incomplete = [dict(item) for item in games]
                incomplete[-1][field] = value
                with patch.object(release, 'load', return_value=incomplete), \
                        patch.object(release, 'certified_runtime', side_effect=AssertionError('must fail before receipts')):
                    for flavor in ('Internal', 'Public'):
                        output = self.root / flavor
                        with self.assertRaisesRegex(ValueError, 'daioujou'):
                            release.build(flavor, '0.3', output, package_format=1)
                        self.assertFalse(output.exists())

    def test_v03_public_requires_five_games_and_never_allows_daioujou_bodies(self):
        directory = self.package('0.3-test')
        self.assertEqual(release.verify_tree(directory, self.games)['games'], list(release.V03_GAMES))
        for name in ('game-data/daioujou/eboot.bin', 'prepared/daioujou/eboot.ktprobe',
                     'source/games/daioujou/data.json', 'runtime/ps4/daioujou.pdb'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                release.validate_public_name(name, self.games, release.V03_GAMES)
        path = directory / 'release.json'
        record = json.loads(path.read_text(encoding='utf-8'))
        record['games'] = list(release.GAMES)
        path.write_text(json.dumps(record), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'release identity'):
            release.verify_tree(directory, self.games)

    def test_ps4_aliases_share_host_certificate_without_guessing_other_paths(self):
        for game in ('ketsui', 'daioujou'):
            for suffix in ('.exe', '.pdb'):
                self.assertEqual(release.certified_name('runtime/ps4/' + game + suffix), 'KetsuiNativeProbe' + suffix)
        self.assertEqual(release.certified_name('runtime/ps4/other.exe'), 'other.exe')
        self.assertEqual(release.certified_name('runtime/xbox360/daioujou.exe'), 'daioujou.exe')

    def test_v02_does_not_allow_a_three_game_identity(self):
        directory = self.package()
        path = directory / 'release.json'
        record = json.loads(path.read_text(encoding='utf-8'))
        record['games'] = list(release.LEGACY_GAMES)
        path.write_text(json.dumps(record), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'release identity'):
            release.verify_tree(directory, self.games)

    def test_public_rejects_rom_source_symbols_and_undeclared_files(self):
        for name in ('game-data/futari/default.xex', 'source/.git/HEAD', 'runtime/fake.pdb',
                     'prepared/ketsui/eboot.ktprobe', 'logs/private.txt', 'README.cpp'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                release.validate_public_name(name, self.games)
        directory = self.package()
        (directory / 'unexpected.bin').write_bytes(b'authored')
        with self.assertRaises(ValueError):
            release.verify_tree(directory, self.games)

    def test_package_changed_content_fails(self):
        directory = self.package()
        (directory / 'config.ini').write_bytes(b'changed')
        with self.assertRaises(ValueError):
            release.verify_tree(directory, self.games)

    def test_copy_checks_expected_hash_and_keeps_existing_destination(self):
        source = self.root / 'input'; source.write_bytes(b'abc')
        target = self.root / 'copy'
        expected = dict(bytes=3, sha256=hashlib.sha256(b'abc').hexdigest())
        entries = []; release.copy_checked(source, target, 'runtime', entries, expected)
        self.assertEqual(target.read_bytes(), b'abc')
        with self.assertRaises(FileExistsError):
            release.copy_checked(source, target, 'runtime', entries, expected)
        source.write_bytes(b'bad')
        with self.assertRaises(ValueError):
            release.copy_checked(source, self.root / 'never-created', 'runtime', [], expected)
        self.assertFalse((self.root / 'never-created').exists())

    def test_complete_data_and_separate_dlc(self):
        source = self.root / 'data'; (source / 'dlc').mkdir(parents=True)
        (source / 'main.bin').write_bytes(b'abc'); (source / 'dlc/old.bin').write_bytes(b'old')
        required = [dict(path='main.bin', bytes=3, sha256=hashlib.sha256(b'abc').hexdigest())]
        release.copy_data(source, self.root / 'output', [], required, {'dlc'})
        self.assertEqual((self.root / 'output/main.bin').read_bytes(), b'abc')
        self.assertFalse((self.root / 'output/dlc').exists())

    def test_sdoj_manifest_copy_leaves_iso_tools_and_music_patch_out(self):
        source = self.root / 'data'; source.mkdir()
        for name in ('main.bin', 'original.iso', 'music-patch.zip', 'tool.exe'):
            (source / name).write_bytes(b'abc')
        required = [dict(path='main.bin', bytes=3, sha256=hashlib.sha256(b'abc').hexdigest())]
        entries = []
        release.copy_data(source, self.root / 'output', entries, required, manifest_only=True)
        self.assertEqual({path.name for path in (self.root / 'output').iterdir()}, {'main.bin'})
        self.assertEqual({path.name for path in source.iterdir()}, {'main.bin', 'original.iso', 'music-patch.zip', 'tool.exe'})

    def test_source_git_is_independent_with_correct_remote(self):
        source = self.root / 'authored-source'; source.mkdir()
        for args in (('init', '-b', 'main'), ('config', 'user.name', 'Fixture'), ('config', 'user.email', 'fixture@example.invalid')):
            subprocess.run(['git', '-C', str(source), *args], check=True, capture_output=True)
        (source / 'README.txt').write_text('authored fixture', encoding='utf-8')
        subprocess.run(['git', '-C', str(source), 'add', 'README.txt'], check=True, capture_output=True)
        subprocess.run(['git', '-C', str(source), 'commit', '-m', 'Authored fixture'], check=True, capture_output=True)
        commit = release.git(source, 'rev-parse', 'HEAD').decode().strip()
        target = self.root / '独立 source'
        with patch.object(release, 'ROOT', source), patch.object(release, 'audit_source'):
            release.source_clone(target, commit)
        self.assertEqual(release.git(target, 'rev-parse', 'HEAD').decode().strip(), commit)
        self.assertEqual(release.git(target, 'remote', 'get-url', 'origin').decode().strip(), release.REMOTE)
        self.assertFalse((target / '.git/objects/info/alternates').exists())

    def test_public_rejects_inclusion_options_before_reading_build_or_rom(self):
        with patch.object(release, 'certified_runtime', side_effect=AssertionError('must not inspect build/ROM')):
            with self.assertRaises(ValueError):
                release.build('Public', '0.1', data={'futari':self.root})
        with self.assertRaises(ValueError):
            release.version_name('../bad', 'Internal')

    def test_both_build_flavors_from_one_authored_fixture(self):
        self.check_build_flavors('0.2-test')

    def test_five_game_build_flavors_and_real_extraction_from_authored_fixture(self):
        self.check_build_flavors('0.3-test')

    def test_v04_build_flavors_keep_six_templates_and_no_public_rom(self):
        self.check_build_flavors('0.4')

    def test_windows_public_flat_zip_preserves_existing_player_files(self):
        def check(root, games, commit, inspect):
            with patch.object(release, 'source_clone', side_effect=AssertionError('No source in new packages')):
                result = release.build('Public', '0.4', root/'releases/windows')
            inspect.assert_not_called()
            self.assertEqual(Path(result['archive']).name, 'EVAC-PC-LABEL-v0.4-win-x64.zip')
            installed = self.root/'玩家目录 with spaces'
            with zipfile.ZipFile(result['archive']) as zipped:
                self.assertIn(release.LAUNCHER, zipped.namelist())
                zipped.extractall(installed)
            record = release.verify_tree(installed, games)
            self.assertEqual(record['format'], 3)
            self.assertEqual(record['sourceUrl'], release.REPOSITORY_URL+'/tree/'+commit)
            self.assertEqual(record['dataGames'], [])
            self.assertFalse(record['symbolsIncluded'])
            self.assertFalse(record['sourceIncluded'])
            names = {item['path'] for item in record['files']}
            self.assertTrue(release.runtime_names(games, selection=release.V03_GAMES, version='0.4',
                                                 package_format=3).issubset(names))
            self.assertNotIn(release.LEGACY_LAUNCHER, names)
            self.assertNotIn('CAVE-PC-Collection.pdb', names)
            self.assertEqual({name for name in names if name.startswith('game-data/')},
                             set(release.placement_sources('0.4')))
            self.assertFalse((installed/'user-data').exists())
            self.assertFalse((installed/'config.ini').exists())
            self.assertFalse((installed/'source').exists())
            self.assertFalse(list(installed.rglob('*.pdb')))
            notice = (installed/'SOURCE.txt').read_text(encoding='utf-8')
            self.assertIn(commit, notice)
            self.assertIn('FOR_AGENT.md', notice)
            self.assertNotIn('附件', notice)
            sentinels = {'config.ini': b'old root config', 'user-data/config.ini': b'player settings',
                         'user-data/futari/save.bin': b'save', 'user-data/sdoj/replay.dat': b'replay',
                         'game-data/ketsui/eboot.bin': b'authored game sentinel',
                         'prepared/ketsui/image': b'prepared', 'cache/marker': b'cache',
                         'logs/run/marker': b'logs', 'source/local-edit.cpp': b'local source'}
            for name, value in sentinels.items():
                file = installed/name; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(value)
            (installed/release.LEGACY_LAUNCHER).write_bytes(b'authored old launcher')
            with zipfile.ZipFile(result['archive']) as zipped:
                zipped.extractall(installed)
            for name, value in sentinels.items():
                self.assertEqual((installed/name).read_bytes(), value, name)
            for item in record['files']:
                self.assertEqual(release.digest(installed/item['path']), item['sha256'])
            # ZIP extraction does not delete pre-existing files. New packages
            # must neither ship nor refresh the retired shortcut target.
            self.assertEqual((installed/release.LEGACY_LAUNCHER).read_bytes(), b'authored old launcher')
        self.check_build_flavors('0.4', check)

    def test_frozen_format2_still_verifies_its_launcher_alias_and_symbols(self):
        def check(root, games, commit, inspect):
            result = release.build('Internal', '0.6', root/'releases/format2',
                                   symbols=True, package_format=2)
            directory = Path(result['directory'])
            record = release.verify_tree(directory, games)
            self.assertEqual(record['format'], 2)
            for suffix in ('.exe', '.pdb'):
                self.assertEqual(release.digest(directory/('EVAC PC LABEL'+suffix)),
                                 release.digest(directory/('CAVE-PC-Collection'+suffix)))
        self.check_build_flavors('0.6', check)

    def test_new_packages_reject_retired_launcher_even_with_matching_inventory(self):
        def check(root, games, commit, inspect):
            for flavor in ('Public', 'Internal'):
                result = release.build(flavor, '0.6', root/'releases'/flavor,
                                       symbols=flavor == 'Internal')
                directory = Path(result['directory'])
                record = release.verify_tree(directory, games)
                for suffix in ('.exe', '.pdb'):
                    name = 'CAVE-PC-Collection'+suffix
                    file = directory/name
                    self.assertFalse(file.exists())
                    original = directory/('EVAC PC LABEL'+suffix)
                    file.write_bytes(original.read_bytes() if original.exists() else b'authored symbols')
                    entry = dict(path=name, role='runtime', bytes=file.stat().st_size, sha256=release.digest(file))
                    self.rewrite_inventory(directory, dict(record, files=[*record['files'], entry]))
                    with self.subTest(flavor=flavor, name=name), self.assertRaises(ValueError):
                        release.verify_tree(directory, games)
                    file.unlink()
                    self.rewrite_inventory(directory, record)
        self.check_build_flavors('0.6', check)

    def test_v06_six_game_program_and_selected_internal_data(self):
        def check(root, games, commit, inspect):
            self.assertEqual(release.release_games('0.6'), release.V06_GAMES)
            self.assertEqual(release.config_name('0.6'), 'user-data/config.ini')
            self.assertEqual(release.certified_name('runtime/ps4/esprade.pdb'), 'KetsuiNativeProbe.pdb')
            with patch.object(release, 'source_clone', side_effect=AssertionError('No source clone')):
                public = release.build('Public', '0.6', root/'releases/public')
                inspect.assert_not_called()
                internal = release.build('Internal', '0.6', root/'releases/internal', data_games=['all'])
            self.assertEqual([call.args[1] for call in inspect.call_args_list], list(release.V06_GAMES))
            for flavor, result in (('Public', public), ('Internal', internal)):
                self.assertEqual(Path(result['archive']).name,
                    'EVAC-PC-LABEL-v0.6-win-x64' + ('-internal' if flavor == 'Internal' else '') + '.zip')
                installed = self.root/('v06 ' + flavor)
                with zipfile.ZipFile(result['archive']) as archive:
                    archive.extractall(installed)
                record = release.verify_tree(installed, games)
                self.assertEqual(record['games'], list(release.V06_GAMES))
                self.assertEqual(record['format'], 3)
                self.assertFalse((installed/release.LEGACY_LAUNCHER).exists())
                self.assertFalse((installed/'CAVE-PC-Collection.pdb').exists())
                self.assertEqual(record['dataGames'], list(release.V06_GAMES) if flavor == 'Internal' else [])
                self.assertEqual((installed/'README.txt').read_text(encoding='utf-8'), 'authored v06 instructions')
                self.assertTrue((installed/'Launch.bat').is_file())
                self.assertFalse((installed/'启动游戏.bat').exists())
                self.assertFalse((installed/'source').exists())
                self.assertFalse((installed/'user-data').exists())
                self.assertFalse((installed/'config.ini').exists())
                self.assertFalse(list(installed.rglob('*.pdb')))
                self.assertEqual(release.digest(installed/'runtime/ps4/esprade.exe'),
                                 release.digest(installed/'runtime/ps4/daioujou.exe'))
                for key in release.V06_GAMES:
                    self.assertTrue((installed/'game-data'/key/release.PLACEMENT_NAME).is_file())
                    self.assertEqual((installed/'game-data'/key/'main.bin').exists(), flavor == 'Internal')
                self.assertFalse((installed/'game-data/esprade/Project.gp4').exists())
            self.assertEqual((root/'game-data/esprade/Project.gp4').read_bytes(), b'authored packaging-only file')
        self.check_build_flavors('0.6', check)

    def test_v07_seven_game_program_and_guwange_only_internal_data(self):
        self.check_seven_game_package('0.7')

    def test_v071_seven_game_program_and_guwange_only_internal_data(self):
        self.check_seven_game_package('0.7.1')

    def test_v072_seven_game_program_and_guwange_only_internal_data(self):
        self.check_seven_game_package('0.7.2')

    def test_v073_seven_game_package_includes_dlc_extractor(self):
        self.assertIn('tools/Extract-Dlc.ps1', release.extraction_sources('0.7.3'))
        self.assertNotIn('tools/Extract-Dlc.ps1', release.extraction_sources('0.7.2'))
        self.check_seven_game_package('0.7.3')

    def test_v074_keeps_seven_game_program_package_and_placement_guides(self):
        self.check_seven_game_package('0.7.4')

    def test_v080_adds_feveron_without_changing_frozen_profiles(self):
        self.assertEqual(release.release_games('0.8.0'), release.V08_GAMES)
        self.assertEqual(len(release.placement_sources('0.7.6')), 7)
        self.assertEqual(len(release.placement_sources('0.8.0')), 8)
        self.assertEqual(release.certified_name('runtime/ps4/feveron.exe'), 'KetsuiNativeProbe.exe')
        self.assertEqual(release.certified_name('runtime/ps4/unknown.exe'), 'unknown.exe')
        def check(root, games, commit, inspect):
            extra = root/'game-data/feveron/container.pkg'
            extra.write_bytes(b'unselected original container')
            with patch.object(release, 'source_clone', side_effect=AssertionError('No source clone')):
                public = release.build('Public', '0.8.0', root/'releases/public')
                inspect.assert_not_called()
                internal = release.build('Internal', '0.8.0', root/'releases/internal', data_games=['feveron'])
            self.assertEqual([call.args[1] for call in inspect.call_args_list], ['feveron'])
            for result, included in ((public, False), (internal, True)):
                directory = Path(result['directory'])
                record = release.verify_tree(directory, games)
                self.assertEqual(record['games'], list(release.V08_GAMES))
                self.assertEqual(record['dataGames'], ['feveron'] if included else [])
                self.assertEqual((directory/'README.txt').read_text(), 'authored v08 instructions')
                self.assertTrue((directory/'runtime/ps4/feveron.exe').is_file())
                self.assertTrue((directory/'game-data/feveron/README.txt').is_file())
                self.assertEqual((directory/'game-data/feveron/main.bin').exists(), included)
                for name in ('game-data/feveron/container.pkg', 'game-data/feveron/Project.gp4',
                             'user-data', 'source', release.LEGACY_LAUNCHER):
                    self.assertFalse((directory/name).exists())
                self.assertFalse(list(directory.rglob('*.pdb')))
                with zipfile.ZipFile(result['archive']) as zipped:
                    self.assertTrue(all(name.isascii() for name in zipped.namelist()))
                    self.assertIn('tools/Extract-PS4.exe', zipped.namelist())
            self.assertEqual(extra.read_bytes(), b'unselected original container')
        self.check_build_flavors('0.8.0', check)

    def test_dlc_source_layout_does_not_change_packaged_paths(self):
        dlc = self.root/'dlc'; base = dlc/'00000002/package'; base.mkdir(parents=True)
        (base/'asset.bin').write_bytes(b'abc')
        canonical = '0000000000000000/435A07D7/00000002/package'
        file = dict(path=canonical+'/asset.bin', bytes=3, sha256=hashlib.sha256(b'abc').hexdigest())
        packages = [dict(id='first', directory=canonical, files=[file])]
        report = dict(optionalContent=[dict(id='first', directory=str(base), state='verified')])
        self.assertEqual(release.content_inputs(dlc, packages, report), {file['path']: base/'asset.bin'})
        (base/'asset.bin').write_bytes(b'bad')
        with self.assertRaises(ValueError): release.content_inputs(dlc, packages, report)

    def check_seven_game_package(self, version):
        self.assertEqual(release.PLACEMENT_NAME, 'README.txt')
        self.assertEqual(release.release_games('0.7'), release.V07_GAMES)
        self.assertEqual(release.release_games('0.6.1'), release.V06_GAMES)
        self.assertEqual(len(release.placement_sources('0.4')), 6)
        self.assertEqual(len(release.placement_sources('0.6.1')), 6)
        self.assertEqual(len(release.placement_sources('0.7')), 7)
        self.assertEqual(set(release.extraction_sources('0.7')),
                         {'tools/Extract-Ps4Pkg.ps1', 'tools/Extract-Xbla.ps1'})
        def check(root, games, commit, inspect):
            (root/'game-data/guwange/container.zip').write_bytes(b'not selected')
            with patch.object(release, 'source_clone', side_effect=AssertionError('No source clone')):
                public=release.build('Public',version,root/'releases/public')
                inspect.assert_not_called()
                internal=release.build('Internal',version,root/'releases/internal',data_games=['guwange'])
            self.assertEqual([call.args[1] for call in inspect.call_args_list], ['guwange'])
            for flavor, result in (('Public',public),('Internal',internal)):
                installed=self.root/('v07 '+flavor)
                with zipfile.ZipFile(result['archive']) as archive:
                    self.assertTrue(all(name.isascii() for name in archive.namelist()))
                    archive.extractall(installed)
                record=release.verify_tree(installed,games)
                self.assertEqual(record['placementGuideName'], 'README.txt')
                self.assertEqual(record['games'],list(release.V07_GAMES))
                self.assertEqual(record['dataGames'],['guwange'] if flavor=='Internal' else [])
                self.assertEqual((installed/'README.txt').read_text(),'authored v07 instructions')
                notice = (installed/'SOURCE.txt').read_text(encoding='utf-8')
                self.assertIn(release.REPOSITORY_URL + '/blob/' + commit + '/FOR_AGENT.md', notice)
                self.assertIn('git clone ' + release.REPOSITORY_URL + '.git', notice)
                self.assertIn('git checkout --detach ' + commit, notice)
                self.assertEqual((installed/'tools/Extract-Xbla.ps1').read_text(),'# authored XBLA helper\n')
                self.assertTrue((installed/'tools/Extract-Ps4Pkg.ps1').is_file())
                for key in release.V07_GAMES:
                    self.assertTrue((installed/'game-data'/key/release.PLACEMENT_NAME).is_file())
                    self.assertEqual((installed/'game-data'/key/'main.bin').exists(),flavor=='Internal' and key=='guwange')
                    self.assertTrue((installed/games[key]['runtimeFiles'][0]).is_file())
                for name in ('source','config.ini','user-data',release.LEGACY_LAUNCHER,'game-data/guwange/container.zip'):
                    self.assertFalse((installed/name).exists())
                self.assertFalse(list(installed.rglob('*.pdb')))
                self.assertEqual(Path(result['archive']).name,
                    'EVAC-PC-LABEL-v'+version+'-win-x64'+('-internal' if flavor=='Internal' else '')+'.zip')
            self.assertEqual((root/'game-data/guwange/container.zip').read_bytes(),b'not selected')
        self.check_build_flavors(version,check)

    def test_frozen_placement_names_are_verified_without_changing_new_packages(self):
        directory=self.package('0.4-dev')
        record=json.loads((directory/'release.json').read_text(encoding='utf-8'))
        record.pop('placementGuideName')
        for item in record['files']:
            if item['path'].startswith('game-data/'):
                old=directory/item['path']
                replacement=old.with_name(release.LEGACY_PLACEMENT_NAME)
                old.rename(replacement)
                item['path']=replacement.relative_to(directory).as_posix()
        (directory/'release.json').write_text(json.dumps(record),encoding='utf-8')
        sums=''.join(item['sha256']+'  '+item['path']+'\n' for item in sorted(record['files'],key=lambda item:item['path']))
        sums+=release.digest(directory/'release.json')+'  release.json\n'
        (directory/'SHA256SUMS.txt').write_text(sums,encoding='utf-8')
        release.verify_tree(directory,self.games)
        self.assertTrue(all(Path(name).name=='README.txt' for name in release.placement_sources('0.7')))

    def test_v07_cannot_release_an_unaccepted_game(self):
        games=release.load(release.ROOT/'games/catalogue.json')
        next(game for game in games if game['id']=='guwange')['communitySupported']=False
        with patch.object(release,'load',return_value=games), \
                patch.object(release,'certified_runtime',side_effect=AssertionError('Must reject before build reads')):
            with self.assertRaisesRegex(ValueError,'integrated with community support'):
                release.build('Public','0.7',self.root/'never-created')
        self.assertFalse((self.root/'never-created').exists())

    def test_windows_internal_selects_base_game_without_dlc_or_other_data(self):
        def check(root, games, commit, inspect):
            (root/'game-data/ketsui/main.bin').unlink()  # Unselected authored input is unavailable.
            (root/'dlc/fixture-dlc.bin').unlink()
            (root/'game-data/futari/extra.iso').write_bytes(b'not selected')
            with patch.object(release, 'source_clone', side_effect=AssertionError('No source clone')):
                result = release.build('Internal', '0.4', root/'releases/futari-only', data_games=['futari'])
            self.assertEqual([call.args[1] for call in inspect.call_args_list], ['futari'])
            self.assertEqual(Path(result['archive']).name, 'EVAC-PC-LABEL-v0.4-win-x64-internal.zip')
            installed = self.root/'内部 解压'
            with zipfile.ZipFile(result['archive']) as zipped:
                zipped.extractall(installed)
            record = release.verify_tree(installed, games)
            self.assertEqual(record['dataGames'], ['futari'])
            self.assertEqual(record['futariContent'], [])
            self.assertEqual({item['path'] for item in record['files'] if item['role'] == 'game-data'},
                             {'game-data/futari/main.bin'})
            self.assertFalse((installed/'source').exists())
            self.assertFalse((installed/'user-data').exists())
            self.assertFalse(list(installed.rglob('*.pdb')))
            self.assertEqual((root/'game-data/futari/extra.iso').read_bytes(), b'not selected')
        self.check_build_flavors('0.4', check)

    def test_windows_internal_selected_dlc_symbols_and_all_selection(self):
        def check(root, games, commit, inspect):
            identity = games['futari']['files'][0]
            games['futari']['content'] = [dict(id='black-label', files=[dict(identity, path='fixture-dlc.bin')]),
                                          dict(id='ver101', files=[dict(identity, path='second-dlc.bin')])]
            (root/'dlc/second-dlc.bin').write_bytes(b'abc')
            (root/'dlc/unselected.bin').write_bytes(b'not selected')
            for all_data in (False, True):
                result = release.build('Internal', '0.4', root/'releases'/str(all_data),
                    data_games=['all'] if all_data else ['futari'],
                    futari_content=['all'] if all_data else ['black-label'], symbols=True)
                record = release.verify_tree(Path(result['directory']), games)
                self.assertTrue(record['symbolsIncluded'])
                self.assertEqual(record['dataGames'], list(release.V03_GAMES) if all_data else ['futari'])
                self.assertEqual(record['futariContent'], ['black-label', 'ver101'] if all_data else ['black-label'])
                names = {item['path'] for item in record['files']}
                self.assertNotIn('game-data/futari/dlc/unselected.bin', names)
                self.assertEqual('game-data/futari/dlc/second-dlc.bin' in names, all_data)
                self.assertIn('EVAC PC LABEL.pdb', names)
                self.assertNotIn(release.LEGACY_LAUNCHER, names)
                self.assertNotIn('CAVE-PC-Collection.pdb', names)
                self.assertNotIn('game-data/daioujou/Project.gp4', names)
                self.assertFalse((Path(result['directory'])/'source').exists())
        self.check_build_flavors('0.4', check)

    def test_windows_internal_binds_selected_ketsui_variant(self):
        def check(root, games, commit, inspect):
            base = games['ketsui']['files']
            games['ketsui']['variants'] = [dict(id='base', files=base),
                dict(id='update-102', files=[dict(base[0], path='updated.bin')])]
            original_inspect = inspect.side_effect
            inspect.side_effect = lambda *args: dict(original_inspect(*args), dataVariant='base')
            result = release.build('Internal', '0.4', root/'releases/ketsui',
                                   data={'ketsui': root/'game-data/ketsui'})
            directory = Path(result['directory'])
            record = release.verify_tree(directory, games)
            self.assertEqual(record['dataVariants'], {'ketsui': 'base'})
            self.assertEqual(record['dataGames'], ['ketsui'])
            self.assertFalse((directory/'game-data/ketsui/updated.bin').exists())
            record['dataVariants']['ketsui'] = 'update-102'
            self.rewrite_inventory(directory, record)
            with self.assertRaisesRegex(ValueError, 'Unselected game data'):
                release.verify_tree(directory, games)
        self.check_build_flavors('0.4', check)

    def test_windows_invalid_selections_fail_before_build_reads(self):
        with patch.object(release, 'certified_runtime', side_effect=AssertionError('No build reads')):
            for options in (dict(data_games=['all']), dict(symbols=True), dict(futari_content=['black-label']),
                            dict(data={'futari': self.root}), dict(futari_dlc=self.root)):
                with self.subTest(options=options), self.assertRaises(ValueError):
                    release.build('Public', '0.4', **options)
            for options in (dict(data_games=['all', 'futari']), dict(data_games=['futari', 'futari']),
                            dict(data_games=['unknown']), dict(data_games='futari'), dict(data_games=[[]]),
                            dict(futari_content=['black-label']), dict(futari_dlc=self.root),
                            dict(data_games=['ketsui'], data={'futari': self.root}), dict(symbols='yes')):
                with self.subTest(options=options), self.assertRaises(ValueError):
                    release.build('Internal', '0.4', **options)
            with self.assertRaises(ValueError):
                release.build('Update', '0.4')

    def test_windows_selected_dlc_missing_or_corrupt_never_creates_output(self):
        def check(root, games, commit, inspect):
            output = root/'releases/invalid'
            for value in (b'bad', None):
                file = root/'dlc/fixture-dlc.bin'
                if value is None: file.unlink()
                else: file.write_bytes(value)
                with self.assertRaisesRegex(ValueError, 'complete selected Futari DLC'):
                    release.build('Internal', '0.4', output, data_games=['futari'], futari_content=['fixture-dlc'])
                self.assertFalse(output.exists())
        self.check_build_flavors('0.4', check)

    def rewrite_inventory(self, directory, record):
        path = directory/'release.json'
        path.write_text(json.dumps(record), encoding='utf-8')
        sums = ''.join(item['sha256']+'  '+item['path']+'\n'
                       for item in sorted(record['files'], key=lambda item: item['path']))
        (directory/'SHA256SUMS.txt').write_text(sums+release.digest(path)+'  release.json\n', encoding='utf-8')

    def test_windows_repacked_inventory_cannot_admit_config_source_or_unselected_data(self):
        def check(root, games, commit, inspect):
            result = release.build('Public', '0.4', root/'releases/public')
            directory = Path(result['directory']); record = release.verify_tree(directory, games)
            for name, role in (('config.ini', 'documentation'), ('user-data/config.ini', 'documentation'),
                               ('source/README.md', 'documentation'), ('runtime/unknown.pdb', 'runtime'),
                               ('game-data/futari/main.bin', 'game-data')):
                file = directory/name; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(b'authored')
                changed = dict(record, files=[*record['files'], dict(path=name, role=role, bytes=8, sha256=release.digest(file))])
                self.rewrite_inventory(directory, changed)
                with self.subTest(name=name), self.assertRaises(ValueError):
                    release.verify_tree(directory, games)
                file.unlink()
            for key, value in (('dataGames', ['all']), ('dataGames', [[]]), ('sourceIncluded', True),
                               ('sourceCommit', None), ('archiveLayout', 'nested'), ('symbolsIncluded', True)):
                self.rewrite_inventory(directory, dict(record, **{key: value}))
                with self.subTest(field=key), self.assertRaises(ValueError):
                    release.verify_tree(directory, games)
            self.rewrite_inventory(directory, record)
            self.assertEqual(release.verify_tree(directory, games)['format'], 3)
        self.check_build_flavors('0.4', check)

    def check_build_flavors(self, version, windows_checks=None):
        # Exercise the actual assembler/ZIP/verifier, not just a hand-written
        # manifest. Certificates and media are authored bytes, never executable.
        root = self.root / 'fixture-project'; root.mkdir()
        text = {'README.txt':'authored source', '.gitignore':'out/\nruntime/\ngame-data/\ndlc/\nreleases/\ncrt/\n*.exe\n*.pdb\n',
                'config/default.ini':'[collection]\nconfig_version=1\n', 'config/README.player.txt':'authored v02 instructions',
                'config/README.player-v03.txt':'authored v03 instructions',
                'config/README.player-v04.txt':'authored v04 instructions',
                'config/README.player-v06.txt':'authored v06 instructions',
                'config/README.player-v07.txt':'authored v07 instructions',
                'config/README.player-v08.txt':'authored v08 instructions',
                'scripts/Extract-Ps4Pkg.ps1':'# authored helper\n',
                'scripts/Extract-Xbla.ps1':'# authored XBLA helper\n',
                'scripts/Extract-Dlc.ps1':'# authored DLC helper\n',
                'Launch.bat':'@echo off\nstart "" "%~dp0EVAC PC LABEL.exe" %*\n', 'launcher/artwork.json':'{}',
                'tools/sdk-source.lock.json':'{}', 'tools/ps4-source.lock.json':'{}'}
        if release.has_ps4_extractor(version):
            dependencies = {name: 'authored ' + name for name in ('PkgTool.exe', 'LibOrbisPkg.dll', 'LICENSE.txt')}
            text.update({'tools/apps/pkgtool-fixture/' + name: value for name, value in dependencies.items()})
            text['tools/ps4-extract.lock.json'] = json.dumps(dict(directory='pkgtool-fixture',
                files={name: hashlib.sha256(value.encode()).hexdigest() for name, value in dependencies.items()}))
            for name in ('config/README.extract-ps4.txt','config/pkgtool-source.txt','config/licenses/GPL-3.0.txt'):
                text[name] = 'authored extraction instructions/license\n'
        for key in release.PLACEMENT_GAMES:
            text['config/game-data/'+key+'.txt']=(release.ROOT/'config/game-data'/ (key+'.txt')).read_text(encoding='utf-8')
        for name, value in text.items():
            target=root/name; target.parent.mkdir(parents=True,exist_ok=True); target.write_text(value,encoding='utf-8',newline='\n')
        for args in (('init','-b','main'),('config','user.name','Fixture'),('config','user.email','fixture@example.invalid'),
                     ('add','.'),('commit','-m','Authored source')):
            subprocess.run(['git','-C',str(root),*args],check=True,capture_output=True)
        commit=release.git(root,'rev-parse','HEAD').decode().strip()
        (root/'third_party/dependency').mkdir(parents=True)
        release.git(root,'update-index','--add','--cacheinfo','160000,'+commit+',third_party/dependency')
        release.git(root,'commit','-m','Authored uninitialized submodule')
        commit=release.git(root,'rev-parse','HEAD').decode().strip()
        identity=dict(path='main.bin',bytes=3,sha256=hashlib.sha256(b'abc').hexdigest())
        selection = release.release_games(version)
        games={key:dict(id=key,files=[identity],content=[],runtimeFiles=[
            'runtime/'+('ps4' if key in ('ketsui', 'daioujou', 'esprade', 'feveron') else 'xbox360')+'/'+key+'.exe',
            'runtime/games/'+key+'/runtime-manifest.ini']) for key in selection}
        games['futari']['content']=[dict(id='fixture-dlc',files=[dict(identity,path='fixture-dlc.bin')])]
        certified={}
        for name in release.runtime_names(games,True,selection,version):
            if name.endswith('.ini'):continue
            base=release.certified_name(name)
            binary=root/'out/bin'/base;binary.parent.mkdir(parents=True,exist_ok=True);binary.write_bytes(('authored '+base).encode())
            certified[base]=dict(source=binary,bytes=binary.stat().st_size,sha256=release.digest(binary))
            staged=root/name;staged.parent.mkdir(parents=True,exist_ok=True);staged.write_bytes(binary.read_bytes())
        for key,game in games.items():
            name=game['runtimeFiles'][0];base=release.certified_name(name)
            (root/game['runtimeFiles'][1]).parent.mkdir(parents=True,exist_ok=True)
            (root/game['runtimeFiles'][1]).write_text('[runtime]\ncontract=1\ngame='+key+'\nsource_commit='+commit+
                '\n[files]\n'+name+'='+certified[base]['sha256']+'\n',encoding='utf-8')
            data=root/'game-data'/key;data.mkdir(parents=True);(data/'main.bin').write_bytes(b'abc')
            if key in ('daioujou', 'esprade', 'feveron'):
                (data/'Project.gp4').write_bytes(b'authored packaging-only file')
        (root/'dlc').mkdir();(root/'dlc/fixture-dlc.bin').write_bytes(b'abc')
        crt=root/'crt';crt.mkdir()
        for name in ('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll','msvcp140_atomic_wait.dll'):
            (crt/name).write_bytes(b'authored CRT fixture')
        def inspected(executable,key,data=None):
            return dict(dataDirectory=str(root/'game-data'/key),contentDirectory=str(root/'dlc'))
        receipts={key:dict(sourceSnapshot=dict(sha256='fixture')) for key in ('launcher','ps4','xbox360')}
        with patch.object(release,'ROOT',root),patch.object(release,'catalogue',return_value=games), \
                patch.object(release,'certified_runtime',return_value=(commit,receipts,certified)), \
                patch.object(release,'redist_directory',return_value=crt),patch.object(release,'license_files',return_value=[]), \
                patch.object(release,'inspect_game',side_effect=inspected) as inspect,patch.object(release,'audit_source'):
            if windows_checks:
                return windows_checks(root, games, commit, inspect)
            flavors=('Internal','Public','Update') if version == '0.4' else ('Internal','Public')
            outputs={flavor:release.build(flavor,version,root/'releases'/flavor,package_format=1) for flavor in flavors}
        internal=Path(outputs['Internal']['archive']).with_suffix('')
        public=Path(outputs['Public']['archive']).with_suffix('')
        self.assertEqual(Path(outputs['Internal']['archive']).name, release.slug(version)+'-v'+version+'-internal.zip')
        self.assertEqual(Path(outputs['Public']['archive']).name, release.slug(version)+'-v'+version+'-public.zip')
        self.assertTrue((internal/'source/.git/HEAD').is_file())
        for flavor in outputs:
            package_root = Path(outputs[flavor]['archive']).with_suffix('')
            source_notice = (package_root/'SOURCE.txt').read_text(encoding='utf-8')
            self.assertIn(release.REPOSITORY_URL + '/tree/' + commit, source_notice)
            self.assertNotIn('github.com/zxl0714/CAVE-PC-Collection', source_notice)
        self.assertTrue((internal/'game-data/futari/dlc/fixture-dlc.bin').is_file())
        self.assertTrue((internal/'game-data/sdoj/main.bin').is_file())
        self.assertEqual((public/'README.txt').read_text(encoding='utf-8'),
                         'authored v04 instructions' if version.startswith('0.4') else 'authored v03 instructions' if 'daioujou' in selection else 'authored v02 instructions')
        if 'daioujou' in selection:
            self.assertTrue((internal/'game-data/daioujou/main.bin').is_file())
            self.assertFalse((internal/'game-data/daioujou/Project.gp4').exists())
            self.assertEqual(release.digest(internal/'runtime/ps4/daioujou.exe'),
                             release.digest(internal/'runtime/ps4/ketsui.exe'))
        self.assertFalse((public/'source').exists())
        if version.startswith('0.4'):
            for key in release.V06_GAMES:
                self.assertEqual({file.name for file in (public/'game-data'/key).iterdir()},{release.PLACEMENT_NAME})
                self.assertTrue((internal/'game-data'/key/release.PLACEMENT_NAME).is_file())
        else:self.assertFalse((public/'game-data').exists())
        self.assertFalse(list(public.rglob('*.pdb')))
        for name in release.runtime_names(games, selection=selection, version=version):
            self.assertEqual(release.digest(internal/name),release.digest(public/name))
        extracted=self.root/'解压验证 with spaces';extracted.mkdir()
        with zipfile.ZipFile(outputs['Internal']['archive']) as zipped:
            zipped.extractall(extracted)
        restored=extracted/internal.name
        self.assertTrue((restored/'source/third_party/dependency').is_dir())
        self.assertEqual(release.git(restored/'source','status','--porcelain'),b'')
        self.assertEqual(release.verify_tree(restored,games)['flavor'],'Internal')
        if version.startswith('0.4'):
            public_extracted=self.root/'Public 解压';public_extracted.mkdir()
            with zipfile.ZipFile(outputs['Public']['archive']) as zipped:zipped.extractall(public_extracted)
            installed=public_extracted/public.name
            self.assertEqual(release.verify_tree(installed,games)['flavor'],'Public')
            self.assertEqual({file.relative_to(installed).as_posix() for file in (installed/'game-data').rglob('*') if file.is_file()},
                             set(release.placement_sources(version)))
            self.assertTrue((installed/release.LAUNCHER).is_file())
            self.assertFalse((installed/release.LEGACY_LAUNCHER).exists())
            with zipfile.ZipFile(outputs['Update']['archive']) as zipped:zipped.extractall(installed)
            self.assertEqual(release.digest(installed/release.LAUNCHER),release.digest(installed/release.LEGACY_LAUNCHER))
            self.assertIn('EVAC PC LABEL.exe',(installed/'启动游戏.bat').read_text(encoding='utf-8'))
            self.assertEqual((installed/'user-data/config.ini').read_text(encoding='utf-8'),text['config/default.ini'])


if __name__ == '__main__':
    unittest.main()
