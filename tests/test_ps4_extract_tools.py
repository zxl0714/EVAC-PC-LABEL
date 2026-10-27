"""Small ROM-free checks for the player extractor dependency and documentation."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import ps4_extract_tools as tools
import release_package as release
import launcher_receipt as receipt


class ExtractToolTests(unittest.TestCase):
    def fixture(self, root, unsafe=False):
        files = {'PkgTool.exe': b'authored executable fixture',
                 'LibOrbisPkg.dll': b'authored library fixture', 'LICENSE.txt': b'authored license fixture'}
        archive = root / 'tools/downloads/PkgTool-0.2.231.zip'
        archive.parent.mkdir(parents=True)
        with zipfile.ZipFile(archive, 'w') as zipped:
            for name, data in files.items():
                zipped.writestr(name, data)
            if unsafe:
                zipped.writestr('../outside.txt', b'authored unsafe member')
        spec = dict(format=1, version='0.2.231', directory='pkgtool-0.2.231',
                    url='https://example.invalid/PkgTool.zip', sha256=tools.digest(archive),
                    files={name: hashlib.sha256(data).hexdigest() for name, data in files.items()})
        (root/'tools/ps4-extract.lock.json').write_text(json.dumps(spec))
        return spec

    def test_prepare_from_pinned_archive_and_reject_modified_install(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); self.fixture(root)
            with patch('urllib.request.urlopen', side_effect=AssertionError('Unexpected network')):
                installed = tools.prepare(root)
                self.assertEqual(tools.verify(root), installed)
                self.assertEqual(tools.prepare(root), installed)
            original = installed/'PkgTool.exe'
            original.write_bytes(b'changed fixture')
            with self.assertRaises(ValueError):
                tools.prepare(root)
            self.assertEqual(original.read_bytes(), b'changed fixture')

    def test_bad_archive_and_traversal_are_rejected(self):
        for unsafe in (False, True):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp); self.fixture(root, unsafe)
                if not unsafe:
                    (root/'tools/downloads/PkgTool-0.2.231.zip').write_bytes(b'corrupt')
                with self.assertRaises(ValueError):
                    tools.prepare(root)
                self.assertFalse((root/'tools/apps/pkgtool-0.2.231').exists())

    def test_staging_contains_all_player_dependencies_without_dev_runtime(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); self.fixture(root); tools.prepare(root)
            for name in ('scripts/Extract-Ps4Pkg.ps1', 'config/README.extract-ps4.txt',
                         'config/pkgtool-source.txt', 'config/licenses/GPL-3.0.txt', 'out/build/bin/Extract-PS4.exe'):
                file = root/name; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(b'authored fixture')
            entry = tools.stage(root/'out/build', root/'player', root)
            self.assertTrue(entry.is_file())
            for name in tools.player_sources(root):
                self.assertTrue((root/'player'/name).is_file(), name)
            self.assertTrue((entry.parent/'Extract-Ps4Pkg.ps1').is_file())
            self.assertFalse((entry.parent/'python.exe').exists())

    def test_new_release_requires_extractor_without_changing_v075(self):
        self.assertEqual(release.extractor_sources('0.7.5'), {})
        files = release.extractor_sources('0.7.6')
        self.assertIn('tools/pkgtool/PkgTool.exe', files)
        self.assertIn('tools/pkgtool/LibOrbisPkg.dll', files)
        self.assertIn('tools/pkgtool/LICENSE.txt', files)
        self.assertIn('tools/pkgtool/GPL-3.0.txt', files)
        self.assertIn('tools/pkgtool/SOURCE.txt', files)
        games = {key: dict(runtimeFiles=[]) for key in release.V07_GAMES}
        self.assertNotIn('tools/Extract-PS4.exe', release.runtime_names(games, selection=release.V07_GAMES, version='0.7.5'))
        self.assertIn('tools/Extract-PS4.exe', release.runtime_names(games, selection=release.V07_GAMES, version='0.7.6'))
        pin = tools.lock()['files']['PkgTool.exe']
        item = dict(path='tools/pkgtool/PkgTool.exe', role='runtime', sha256=pin)
        record = dict(version='0.7.6', flavor='Public')
        release.validate_windows_item(item, record, games, release.V07_GAMES, {}, set())
        for changed in (dict(item, role='documentation'), dict(item, sha256='0'*64), dict(item,path='tools/unknown.exe')):
            with self.assertRaises(ValueError):
                release.validate_windows_item(changed, record, games, release.V07_GAMES, {}, set())

    def test_player_docs_point_to_the_double_click_entry(self):
        interface = (ROOT/'tools/ps4-extract/main.cpp').read_text(encoding='utf-8')
        self.assertIn('bool chinese = false;', interface)
        self.assertNotIn('GetUserDefaultUILanguage', interface)
        for name in ('README.md', 'README.en.md', 'docs/GAME_DATA.md', 'docs/GAME_DATA.en.md',
                     'config/README.player-v07.txt', 'config/game-data/ketsui.txt',
                     'config/game-data/daioujou.txt', 'config/game-data/esprade.txt'):
            with self.subTest(name=name):
                text = (ROOT/name).read_text(encoding='utf-8')
                self.assertIn('tools/Extract-PS4.exe', text)
        self.assertIn('extract.log', (ROOT/'config/README.extract-ps4.txt').read_text(encoding='utf-8'))

    def test_launcher_receipt_requires_and_verifies_all_three_pairs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); state = root/'receipt.json'; outputs = []
            for name in (receipt.NAME, 'cave_inspect', 'Extract-PS4'):
                for extension in ('.exe', '.pdb'):
                    file = root/'out/common/bin'/(name + extension)
                    file.parent.mkdir(parents=True, exist_ok=True)
                    file.write_bytes(b'authored build fixture')
                    outputs.append(dict(path=file.relative_to(root).as_posix(), sha256=tools.digest(file)))
            record = dict(format=1, backend='launcher', sourceSnapshot={'fixture': True},
                          rootStaged=False, outputs=outputs)
            state.write_text(json.dumps(record))
            with patch.object(receipt, 'ROOT', root), patch.object(receipt, 'RECEIPT', state), \
                 patch.object(receipt, 'snapshot', return_value={'fixture': True}):
                self.assertEqual(len(receipt.verify()['outputs']), 6)
                state.write_text(json.dumps(dict(record, outputs=outputs[:4])))
                with self.assertRaisesRegex(ValueError, 'Incomplete'):
                    receipt.verify()
                state.write_text(json.dumps(record))
                (root/'out/common/bin/Extract-PS4.exe').write_bytes(b'changed fixture')
                with self.assertRaisesRegex(ValueError, 'differs'):
                    receipt.verify()

    @unittest.skipUnless(os.name == 'nt' and (ROOT/'out/common/bin/ps4_extract_tests.exe').is_file(),
                         'Build ps4_extract_tests for the authored process fixture')
    def test_advanced_script_finds_bundled_tool_and_accepts_resource_only_patch(self):
        with tempfile.TemporaryDirectory(prefix='evac-extract-script-') as tmp:
            root = Path(tmp); directory = root/'tools'; dependencies = directory/'pkgtool'
            dependencies.mkdir(parents=True)
            shutil.copy2(ROOT/'scripts/Extract-Ps4Pkg.ps1', directory)
            shutil.copy2(ROOT/'out/common/bin/ps4_extract_tests.exe', dependencies/'PkgTool.exe')
            (dependencies/'LibOrbisPkg.dll').write_bytes(b'authored sentinel')
            data = bytearray(0x100)
            for at, value in ((0,0x7f434e54),(0x10,2),(0x18,0x40),(0x40,0x1000),(0x60,0x1260)):
                data[at:at+4] = value.to_bytes(4, 'big')
            (root/'patch.pkg').write_bytes(data)
            result = subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-File',
                str(directory/'Extract-Ps4Pkg.ps1'), '-InputPkg','patch.pkg', '-OutputDir','output 中文'],
                cwd=root, capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            game = root/'output 中文/uroot'
            self.assertTrue((game/'ps4data_patch_body.bin').is_file())
            self.assertFalse((game/'eboot.bin').exists())
            self.assertTrue((game/'sce_sys/changeinfo/changeinfo.xml').is_file())
            self.assertEqual((root/'patch.pkg').read_bytes(), data)


if __name__ == '__main__':
    unittest.main()
