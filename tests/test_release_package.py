import hashlib
import json
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
        self.games = {key: dict(runtimeFiles=['runtime/fake.dll']) for key in release.GAMES}

    def tearDown(self):
        self.temporary.cleanup()

    def package(self):
        directory = self.root / '发行 中文 space'
        directory.mkdir()
        entries = []
        for name in release.runtime_names(self.games) | {'config.ini', 'README.txt'}:
            release.text_new(directory / name, 'authored fixture\n', 'config' if name == 'config.ini' else 'runtime', entries)
        payload = [dict(item, path=item['path'].relative_to(directory).as_posix()) for item in entries]
        record = dict(format=1, version='0.1-test', flavor='Public', games=list(release.GAMES), files=payload)
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
        # Exercise the actual assembler/ZIP/verifier, not just a hand-written
        # manifest. Certificates and media are authored bytes, never executable.
        root = self.root / 'fixture-project'; root.mkdir()
        text = {'README.txt':'authored source', '.gitignore':'out/\nruntime/\ngame-data/\ndlc/\nreleases/\ncrt/\n*.exe\n*.pdb\n',
                'config/default.ini':'[collection]\nconfig_version=1\n', 'config/README.player.txt':'authored instructions',
                '启动游戏.bat':'@echo off\n', 'launcher/artwork.json':'{}',
                'tools/sdk-source.lock.json':'{}', 'tools/ps4-source.lock.json':'{}'}
        for name, value in text.items():
            target=root/name; target.parent.mkdir(parents=True,exist_ok=True); target.write_text(value,encoding='utf-8')
        for args in (('init','-b','main'),('config','user.name','Fixture'),('config','user.email','fixture@example.invalid'),
                     ('add','.'),('commit','-m','Authored source')):
            subprocess.run(['git','-C',str(root),*args],check=True,capture_output=True)
        commit=release.git(root,'rev-parse','HEAD').decode().strip()
        (root/'third_party/dependency').mkdir(parents=True)
        release.git(root,'update-index','--add','--cacheinfo','160000,'+commit+',third_party/dependency')
        release.git(root,'commit','-m','Authored uninitialized submodule')
        commit=release.git(root,'rev-parse','HEAD').decode().strip()
        identity=dict(path='main.bin',bytes=3,sha256=hashlib.sha256(b'abc').hexdigest())
        games={key:dict(id=key,files=[identity],content=[],runtimeFiles=[
            'runtime/'+('ps4' if key=='ketsui' else 'xbox360')+'/'+key+'.exe',
            'runtime/games/'+key+'/runtime-manifest.ini']) for key in release.GAMES}
        games['futari']['content']=[dict(id='fixture-dlc',files=[dict(identity,path='fixture-dlc.bin')])]
        certified={}
        for name in release.runtime_names(games,True):
            if name.endswith('.ini'):continue
            base=Path(name).name
            if name.startswith('runtime/ps4/ketsui.'):base='KetsuiNativeProbe'+Path(name).suffix
            binary=root/'out/bin'/base;binary.parent.mkdir(parents=True,exist_ok=True);binary.write_bytes(('authored '+base).encode())
            certified[base]=dict(source=binary,bytes=binary.stat().st_size,sha256=release.digest(binary))
            staged=root/name;staged.parent.mkdir(parents=True,exist_ok=True);staged.write_bytes(binary.read_bytes())
        for key,game in games.items():
            name=game['runtimeFiles'][0];base=Path(name).name
            if key=='ketsui':base='KetsuiNativeProbe.exe'
            (root/game['runtimeFiles'][1]).parent.mkdir(parents=True,exist_ok=True)
            (root/game['runtimeFiles'][1]).write_text('[runtime]\ncontract=1\ngame='+key+'\nsource_commit='+commit+
                '\n[files]\n'+name+'='+certified[base]['sha256']+'\n',encoding='utf-8')
            data=root/'game-data'/key;data.mkdir(parents=True);(data/'main.bin').write_bytes(b'abc')
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
            outputs={flavor:release.build(flavor,'0.1-test',root/'releases'/flavor) for flavor in ('Internal','Public')}
        internal=Path(outputs['Internal']['archive']).with_suffix('')
        public=Path(outputs['Public']['archive']).with_suffix('')
        self.assertEqual(Path(outputs['Internal']['archive']).name, 'CAVE-PC-Collection-v0.1-test-internal.zip')
        self.assertEqual(Path(outputs['Public']['archive']).name, 'CAVE-PC-Collection-v0.1-test-public.zip')
        self.assertTrue((internal/'source/.git/HEAD').is_file())
        self.assertTrue((internal/'game-data/futari/dlc/fixture-dlc.bin').is_file())
        self.assertFalse((public/'source').exists());self.assertFalse((public/'game-data').exists())
        self.assertFalse(list(public.rglob('*.pdb')))
        for name in release.runtime_names(games):
            self.assertEqual(release.digest(internal/name),release.digest(public/name))
        extracted=self.root/'解压验证 with spaces';extracted.mkdir()
        with zipfile.ZipFile(outputs['Internal']['archive']) as zipped:
            zipped.extractall(extracted)
        restored=extracted/internal.name
        self.assertTrue((restored/'source/third_party/dependency').is_dir())
        self.assertEqual(release.git(restored/'source','status','--porcelain'),b'')
        self.assertEqual(release.verify_tree(restored,games)['flavor'],'Internal')


if __name__ == '__main__':
    unittest.main()
