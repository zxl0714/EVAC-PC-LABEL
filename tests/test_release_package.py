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
        for name in release.runtime_names(self.games, selection=selection) | optional_config | {'README.txt'}:
            release.text_new(directory / name, 'authored fixture\n', 'config' if name == config else 'runtime', entries)
        if flavor != 'Update':
            for name, source in release.placement_sources(version).items():
                release.copy_checked(source, directory / name, 'documentation', entries)
        payload = [dict(item, path=item['path'].relative_to(directory).as_posix()) for item in entries]
        record = dict(format=1, version=version, flavor=flavor, games=list(selection), files=payload)
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
            release.build('Update', '0.3.1', data={'sdoj': self.root})

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

    def test_v04_public_templates_are_exact_guides_not_a_game_data_allowlist(self):
        directory=self.package('0.4-dev')
        guides=release.placement_sources('0.4-dev')
        self.assertEqual(len(guides),6)
        self.assertEqual({file.relative_to(directory).as_posix() for file in (directory/'game-data').rglob('*') if file.is_file()},set(guides))
        for name in ('game-data/ketsui/eboot.bin','game-data/futari/notes.txt','game-data/unknown/放置说明.txt'):
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
                            release.build(flavor, '0.3', output)
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
        self.check_build_flavors('0.4-test')

    def check_build_flavors(self, version):
        # Exercise the actual assembler/ZIP/verifier, not just a hand-written
        # manifest. Certificates and media are authored bytes, never executable.
        root = self.root / 'fixture-project'; root.mkdir()
        text = {'README.txt':'authored source', '.gitignore':'out/\nruntime/\ngame-data/\ndlc/\nreleases/\ncrt/\n*.exe\n*.pdb\n',
                'config/default.ini':'[collection]\nconfig_version=1\n', 'config/README.player.txt':'authored v02 instructions',
                'config/README.player-v03.txt':'authored v03 instructions',
                'config/README.player-v04.txt':'authored v04 instructions',
                '启动游戏.bat':'@echo off\n', 'launcher/artwork.json':'{}',
                'tools/sdk-source.lock.json':'{}', 'tools/ps4-source.lock.json':'{}'}
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
            'runtime/'+('ps4' if key in ('ketsui', 'daioujou') else 'xbox360')+'/'+key+'.exe',
            'runtime/games/'+key+'/runtime-manifest.ini']) for key in selection}
        games['futari']['content']=[dict(id='fixture-dlc',files=[dict(identity,path='fixture-dlc.bin')])]
        certified={}
        for name in release.runtime_names(games,True,selection):
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
            if key == 'daioujou':
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
                patch.object(release,'inspect_game',side_effect=inspected),patch.object(release,'audit_source'):
            outputs={flavor:release.build(flavor,version,root/'releases'/flavor) for flavor in ('Internal','Public')}
        internal=Path(outputs['Internal']['archive']).with_suffix('')
        public=Path(outputs['Public']['archive']).with_suffix('')
        self.assertEqual(Path(outputs['Internal']['archive']).name, 'CAVE-PC-Collection-v'+version+'-internal.zip')
        self.assertEqual(Path(outputs['Public']['archive']).name, 'CAVE-PC-Collection-v'+version+'-public.zip')
        self.assertTrue((internal/'source/.git/HEAD').is_file())
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
            for key in release.PLACEMENT_GAMES:
                self.assertEqual({file.name for file in (public/'game-data'/key).iterdir()},{release.PLACEMENT_NAME})
                self.assertTrue((internal/'game-data'/key/release.PLACEMENT_NAME).is_file())
        else:self.assertFalse((public/'game-data').exists())
        self.assertFalse(list(public.rglob('*.pdb')))
        for name in release.runtime_names(games, selection=selection):
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


if __name__ == '__main__':
    unittest.main()
