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
        cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('OUTPUT_NAME "'+product.NAME+'"', cmake)
        self.assertIn('project(EVACPCLabel VERSION 0.4.0', cmake)
        self.assertIn('"%~dp0'+product.LAUNCHER+'" %*', (ROOT/'启动游戏.bat').read_text(encoding='utf-8'))
        for file in ('backends/ps4/collection_config.cpp', 'backends/xbox360/host.cpp'):
            text = (ROOT/file).read_text(encoding='utf-8')
            self.assertIn('"'+product.NAME+' — ', text)
            self.assertNotIn('CAVE PC Collection — ', text)
        self.assertIn('kWindowTitle[]=L"'+product.NAME+'"', (ROOT/'launcher/main.cpp').read_text(encoding='utf-8'))

    def test_versioned_names_keep_frozen_archives_and_native_alias_mapping(self):
        for version in ('0.1','0.2','0.3','0.3.1','0.3.2'):
            self.assertEqual(product.launcher(version), 'CAVE-PC-Collection.exe')
            self.assertEqual(product.slug(version), 'CAVE-PC-Collection')
        self.assertEqual(release.DEFAULT_VERSION, '0.4')
        self.assertEqual(release.certified_name(product.LEGACY_LAUNCHER), product.LAUNCHER)
        self.assertEqual(release.certified_name('other.exe'), 'other.exe')
        self.assertEqual(release.UPDATE_PRESERVED, ('config.ini','user-data/','game-data/','prepared/','cache/','logs/','source/'))
