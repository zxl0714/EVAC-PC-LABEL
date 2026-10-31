from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from ps2_translation_units import make_units


class Ps2TranslationUnitTests(unittest.TestCase):
    def test_every_body_included_once_and_unchanged_batches_keep_timestamps(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'generated space';source.mkdir();output=root/'units'
            for name in ('register_functions.cpp','one.cpp','two.cpp','three.cpp'):
                (source/name).write_text('// fixture',encoding='ascii')
            (source/'ps2_recompiled_functions.h').write_text('// fixture',encoding='ascii')
            receipt=make_units(source,output,2)
            self.assertEqual((receipt['sourceFiles'],receipt['translationUnits']),(4,2))
            paths=sorted(output.glob('batch_*.cpp'))
            text=''.join(path.read_text() for path in paths)
            for name in ('register_functions.cpp','one.cpp','two.cpp','three.cpp'):
                self.assertEqual(text.count('/'+name+'"'),1)
            self.assertNotIn('functions.h',text)
            stamp=paths[0].stat().st_mtime_ns
            (source/'one.cpp').write_text('// changed body',encoding='ascii')
            make_units(source,output,2)
            self.assertEqual(paths[0].stat().st_mtime_ns,stamp)
            # Previously emitted excess batches stay on disk but leave the
            # active CMake list; they cannot compile stale guest definitions.
            (source/'three.cpp').unlink();(source/'two.cpp').unlink()
            receipt=make_units(source,output,2)
            self.assertEqual(receipt['translationUnits'],1)
            self.assertTrue(paths[1].exists())
            self.assertNotIn(paths[1].as_posix(),(output/'sources.cmake').read_text())

    def test_missing_inventory_or_recursive_output_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)
            with self.assertRaises(ValueError):make_units(source,source/'units')
            (source/'register_functions.cpp').touch();(source/'ps2_recompiled_functions.h').touch()
            with self.assertRaises(ValueError):make_units(source,source)
            with self.assertRaises(ValueError):make_units(source,source/'units',0)


if __name__ == '__main__':
    unittest.main()
