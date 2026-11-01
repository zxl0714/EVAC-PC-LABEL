from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from ps2_module_build import prepare


class Ps2ModuleBuildTests(unittest.TestCase):
    def fixture(self,root):
        generated=root/'generated';generated.mkdir()
        (generated/'register_functions.cpp').write_text('// authored fixture')
        (generated/'ps2_recompiled_functions.h').write_text('// authored fixture')
        module={'id':1,'address':'0x649800','textSize':'0x100','dataSize':'0x20',
                'bssSize':'0x20','sha256':'a'*64}
        values={
            'metadata.json':{'format':1,'bootSha256':'b'*64,'modules':[module]},
            'prepared.json':{'bootSha256':'b'*64,'modules':[{'module_id':1,'sha256':'a'*64,'analysisSha256':'c'*64}]},
            'receipt.json':{'elfSha256':'c'*64,'summary':dict.fromkeys(['errors','decodeFailures','unhandledInstructions','criticalGuestFallbacks'],0)},
            'build.json':{'format':1,'modules':[{'id':1,'generated':str(generated),'receipt':str(root/'receipt.json'),'prepared':str(root/'prepared.json')}]},
        }
        for name,value in values.items():(root/name).write_text(json.dumps(value))
        return values

    def test_registration_is_bound_to_boot_and_uses_separate_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);self.fixture(root)
            result=prepare(root/'build.json',root/'metadata.json',root/'output')
            self.assertEqual(result['modules'],1)
            code=(root/'output/registration.cpp').read_text()
            self.assertIn('PS2_REGISTER_GAME_OVERRIDE',code)
            self.assertIn('0x95b4197du',code)
            self.assertIn('g_ps2Stage01FunctionTable',code)
            cmake=(root/'output/modules.cmake').read_text()
            self.assertIn('UNITY_BUILD OFF',cmake)
            self.assertIn('SKIP_UNITY_BUILD_INCLUSION ON',cmake)
            self.assertNotIn('target_compile_definitions(ps2_espgaluda_probe',cmake)

    def test_mismatched_image_or_failed_generation_rejected(self):
        for failure in ('elfSha256','criticalGuestFallbacks'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);values=self.fixture(root);receipt=values['receipt.json']
                if failure=='elfSha256':receipt[failure]='d'*64
                else:receipt['summary'][failure]=1
                (root/'receipt.json').write_text(json.dumps(receipt))
                with self.assertRaises(ValueError):prepare(root/'build.json',root/'metadata.json',root/'output')

    def test_entry_alias_covers_table_and_definition_without_changing_bodies(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);self.fixture(root);generated=root/'generated'
            (generated/'ps2_recompiled_functions.h').write_text('void root_same(uint8_t*, Ctx*, Runtime*);')
            (generated/'root_same.cpp').write_text('void root_same() {}')
            (generated/'register_functions.cpp').write_text('auto table = &root_same;')
            prepare(root/'build.json',root/'metadata.json',root/'output')
            cmake=(root/'output/modules.cmake').read_text()
            self.assertIn('root_same=espgaluda_stage_01_root_same',cmake)
            self.assertIn('SKIP_PRECOMPILE_HEADERS ON',cmake)
            self.assertEqual((generated/'root_same.cpp').read_text(),'void root_same() {}')

    def test_mode_ids_and_duplicate_stage_banks_rejected(self):
        for ids in ([16],[1,1]):
            with self.subTest(ids=ids),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);values=self.fixture(root);build=values['build.json']
                original=build['modules'][0];build['modules']=[dict(original,id=number) for number in ids]
                (root/'build.json').write_text(json.dumps(build))
                with self.assertRaises(ValueError):prepare(root/'build.json',root/'metadata.json',root/'output')

    def test_new_bank_scopes_entry_for_the_target_and_pch_without_reading_bodies(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);values=self.fixture(root);generated=root/'generated'
            (generated/'ps2_recompiled_functions.h').write_text('void root_same(uint8_t*, Ctx*, Runtime*);')
            body=generated/'root_same.cpp';body.write_text('void root_same() {}')
            values['build.json']['modules'][0]['globalRootAliases']=True
            (root/'build.json').write_text(json.dumps(values['build.json']))
            read=Path.read_text
            def guarded(path,*args,**kwargs):
                if path==body:raise AssertionError('Global aliases must not scan function bodies')
                return read(path,*args,**kwargs)
            with mock.patch.object(Path,'read_text',guarded):
                prepare(root/'build.json',root/'metadata.json',root/'output')
            code=(root/'output/modules.cmake').read_text()
            self.assertIn('root_same=espgaluda_stage_01_root_same',code)
            self.assertNotIn('SKIP_PRECOMPILE_HEADERS ON',code)
            self.assertEqual(body.read_text(),'void root_same() {}')


if __name__=='__main__':unittest.main()
