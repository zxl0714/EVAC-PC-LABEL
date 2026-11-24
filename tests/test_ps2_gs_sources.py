import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import ps2_gs_sources as gs


class GsSourcesTest(unittest.TestCase):
    def fixture(self, root):
        lock = []
        for index, name in enumerate(gs.NAMES, 1):
            path = root / '.deps/ps2' / name
            path.mkdir(parents=True)
            (path / (name + '.cpp')).write_text(name, encoding='utf-8')
            (path / '.git').mkdir()
            (path / '.git/private').write_text('not copied', encoding='utf-8')
            lock.append({'name': name, 'revision': str(index) * 40, 'url': 'unused'})
        source_patch = root / gs.RENDERER_PATCH
        source_patch.parent.mkdir(parents=True)
        source_patch.write_text('--- a/Granite/vulkan/wsi.cpp\n+++ b/Granite/vulkan/wsi.cpp\n'
                                '@@ -1 +1 @@\n-GraniteGS\n+Patched GraniteGS\n', encoding='utf-8')
        wsi_dir = root / '.deps/ps2/GraniteGS/vulkan'
        wsi_dir.mkdir()
        (wsi_dir / 'wsi.cpp').write_text('GraniteGS\n', encoding='utf-8')
        return lock

    def prepare(self, root, output, lock, mismatch=False):
        by_name = {item['name']: item['revision'] for item in lock}
        pins = {'HEAD:Granite': by_name['GraniteGS'],
                'HEAD:third_party/volk': by_name['VolkGS'],
                'HEAD:third_party/khronos/vulkan-headers': by_name['VulkanHeadersGS']}
        with patch.object(gs, 'locked_dependencies', return_value=lock), \
             patch.object(gs, 'verify_checkout') as verify, \
             patch.object(gs, 'git', side_effect=lambda path, command, ref: '0' * 40 if mismatch else pins[ref]):
            result = gs.prepare(output, root)
            self.assertEqual(verify.call_count, 4)
            return result

    def test_view_keeps_sources_and_validates_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock = self.fixture(root)
            view = self.prepare(root, root / 'build', lock)
            self.assertFalse((view / '.git').exists())
            self.assertTrue((view / 'Granite/third_party/volk/VolkGS.cpp').exists())
            self.assertEqual(view, self.prepare(root, root / 'build', lock))
            receipt = json.loads((view / 'collection-source.json').read_text(encoding='utf-8'))
            self.assertEqual(receipt['patchSha256'],
                             hashlib.sha256((root / gs.RENDERER_PATCH).read_bytes()).hexdigest())
            self.assertEqual((view / 'Granite/vulkan/wsi.cpp').read_text(encoding='utf-8'), 'Patched GraniteGS\n')
            self.assertEqual((root / '.deps/ps2/GraniteGS/vulkan/wsi.cpp').read_text(encoding='utf-8'),
                             'GraniteGS\n')
            self.assertEqual(len(receipt['files']), 5)
            (view / 'ParallelGS.cpp').write_text('modified', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'differs'):
                self.prepare(root, root / 'build', lock)
            self.assertEqual((view / 'ParallelGS.cpp').read_text(), 'modified')
            self.assertEqual((root / '.deps/ps2/ParallelGS/ParallelGS.cpp').read_text(), 'ParallelGS')

    def test_parent_pin_mismatch_leaves_output_absent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock = self.fixture(root)
            with self.assertRaisesRegex(ValueError, 'Inconsistent'):
                self.prepare(root, root / 'build', lock, mismatch=True)
            self.assertFalse((root / 'build').exists())

    def test_dirty_input_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock = self.fixture(root)
            with patch.object(gs, 'locked_dependencies', return_value=lock), \
                 patch.object(gs, 'verify_checkout', side_effect=ValueError('dirty')):
                with self.assertRaisesRegex(ValueError, 'dirty'):
                    gs.prepare(root / 'build', root)
            self.assertFalse((root / 'build').exists())


if __name__ == '__main__':
    unittest.main()
