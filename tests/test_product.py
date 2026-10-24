from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import product
import release_package as release


class ProductTests(unittest.TestCase):
    def test_current_build_executable_bat_and_windows_captions_agree(self):
        self.assertEqual(product.NAME, 'EVAC PC LABEL')
        self.assertEqual(product.LAUNCHER, 'EVAC PC LABEL.exe')
        self.assertEqual(product.REMOTE, 'git@github.com:zxl0714/EVAC-PC-LABEL.git')
        self.assertEqual(product.REPOSITORY_URL, 'https://github.com/zxl0714/EVAC-PC-LABEL')
        cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('OUTPUT_NAME "'+product.NAME+'"', cmake)
        self.assertIn('project(EVACPCLabel VERSION '+product.VERSION+' ', cmake)
        release_script = (ROOT/'scripts/Release.ps1').read_text(encoding='utf-8')
        self.assertIn("[string]$Version='"+product.VERSION+"'", release_script)
        self.assertIn('"%~dp0'+product.LAUNCHER+'" %*', (ROOT/'Launch.bat').read_text(encoding='utf-8'))
        ignored = (ROOT/'.gitignore').read_text(encoding='utf-8').splitlines()
        self.assertIn('/'+product.LAUNCHER, ignored)
        self.assertIn('/'+product.NAME+'.pdb', ignored)
        for file in ('backends/ps4/collection_config.cpp', 'backends/xbox360/host.cpp'):
            text = (ROOT/file).read_text(encoding='utf-8')
            self.assertIn('"'+product.NAME+' — ', text)
            self.assertNotIn('CAVE PC Collection — ', text)
        self.assertIn('kWindowTitle[]=L"'+product.NAME+'"', (ROOT/'launcher/main.cpp').read_text(encoding='utf-8'))

    def test_versioned_names_keep_frozen_archives_and_native_alias_mapping(self):
        for version in ('0.1','0.2','0.3','0.3.1','0.3.2'):
            self.assertEqual(product.launcher(version), 'CAVE-PC-Collection.exe')
            self.assertEqual(product.slug(version), 'CAVE-PC-Collection')
        self.assertEqual(release.DEFAULT_VERSION, '0.7.1')
        self.assertEqual(release.certified_name(product.LEGACY_LAUNCHER), product.LAUNCHER)
        self.assertEqual(release.certified_name('other.exe'), 'other.exe')
        self.assertEqual(release.UPDATE_PRESERVED, ('config.ini','user-data/','game-data/','prepared/','cache/','logs/','source/'))
