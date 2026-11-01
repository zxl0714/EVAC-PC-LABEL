from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

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

    def test_batch_order_matches_path_order_with_mixed_case_names(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'generated';source.mkdir()
            names=['FUN_upper.cpp','entry_lower.cpp','register_functions.cpp']
            for name in names:(source/name).write_text('// fixture')
            (source/'ps2_recompiled_functions.h').write_text('')
            output=Path(directory)/'units';make_units(source,output,1)
            ordered=sorted(source/name for name in names)
            for index,path in enumerate(ordered):
                self.assertIn(path.as_posix(),(output/f'batch_{index:04}.cpp').read_text())

    def test_entry_references_found_outside_definition_batch(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'generated';source.mkdir()
            bodies={'a.cpp':'void root_same() {}',
                    'b.cpp':'void call() { root_same(); }',
                    'c.cpp':'void root_same_other() {}',
                    'register_functions.cpp':'auto entry = &root_same;'}
            for name,text in bodies.items():(source/name).write_text(text)
            (source/'ps2_recompiled_functions.h').write_text('void root_same();')
            result=make_units(source,root/'units',1,symbols=['root_same'])
            users=[Path(p).name for p in result['symbolUsers']['root_same']]
            self.assertEqual(users,['batch_0000.cpp','batch_0001.cpp','batch_0003.cpp'])
            self.assertEqual({p.name:p.read_text() for p in source.glob('*.cpp')},bodies)

    def test_reference_cache_reuses_unchanged_bodies_and_invalidates_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'generated';source.mkdir();output=root/'units'
            (source/'register_functions.cpp').write_text('root_one();')
            body=source/'body.cpp';body.write_text('root_two();')
            (source/'ps2_recompiled_functions.h').write_text('')
            expected=make_units(source,output,2,symbols=['root_one','root_two'])
            read_text=Path.read_text
            def cached_read(path,*args,**kwargs):
                if path.parent==source and path.suffix=='.cpp':
                    raise AssertionError('Unchanged generated body was rescanned')
                return read_text(path,*args,**kwargs)
            with patch.object(Path,'read_text',cached_read):
                self.assertEqual(make_units(source,output,2,symbols=['root_one','root_two']),expected)
            body.write_text('root_one(); root_two(); // changed')
            updated=make_units(source,output,1,symbols=['root_one','root_two'])
            self.assertEqual(len(updated['symbolUsers']['root_one']),2)


if __name__ == '__main__':
    unittest.main()
