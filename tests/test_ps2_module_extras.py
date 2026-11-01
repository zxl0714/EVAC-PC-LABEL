from pathlib import Path
import json
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from project import digest
from ps2_module_extras import prepare


class Ps2ModuleExtrasTests(unittest.TestCase):
    def fixture(self,root):
        base=root/'base';extra=root/'extra';base.mkdir();extra.mkdir()
        (base/'register_functions.cpp').write_text('g_ps2RecompiledFunctionTable[0] = old; // 0x730880\n')
        (extra/'register_functions.cpp').write_text(
            'g_ps2RecompiledFunctionTable[0] = root_00730880; // 0x730880\n'
            'g_ps2RecompiledFunctionTable[4] = fresh_0x730890; // 0x730890\n'
            'g_ps2RecompiledFunctionTable[5] = fresh_0x730890; // 0x730894\n')
        (extra/'fresh_0x730890.cpp').write_text('// authored body\n')
        mapping=root/'map.csv';mapping.write_text('Name,Start,End,Size\nfresh,0x730890,0x730898,8\n')
        receipt=dict(elfSha256='a'*64,functionMapSha256=digest(mapping),
                     summary=dict.fromkeys(('errors','decodeFailures','unhandledInstructions','criticalGuestFallbacks'),0))
        (root/'receipt.json').write_text(json.dumps(receipt))
        (root/'base-receipt.json').write_text(json.dumps(receipt))
        spec=dict(format=1,id=16,base=str(base),generated=str(extra),map=str(mapping),
                  receipt=str(root/'receipt.json'),baseReceipt=str(root/'base-receipt.json'))
        manifest=root/'build.json';manifest.write_text(json.dumps(spec))
        return manifest

    def test_only_new_bodies_and_their_resume_points_are_linked(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);manifest=self.fixture(root)
            result=prepare(manifest,root/'output')
            self.assertEqual((result['functions'],result['entries']),(1,2))
            code=(root/'output/registration.cpp').read_text()
            self.assertIn('{0x730894u,fresh_0x730890}',code)
            self.assertIn('previous && previous!=entry.function',code)
            cmake=(root/'output/extras.cmake').read_text()
            self.assertIn('fresh_0x730890.cpp',cmake)
            self.assertNotIn('register_functions.cpp',cmake)
            self.assertNotIn('root_00730880',code)

    def test_existing_dispatch_slots_are_never_replaced(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);manifest=self.fixture(root)
            with (root/'base/register_functions.cpp').open('a') as stream:
                stream.write('g_ps2RecompiledFunctionTable[4] = old; // 0x730890\n')
            with self.assertRaisesRegex(ValueError,'overlap'):prepare(manifest,root/'output')

    def test_boot_and_arrange_use_their_own_tables(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);manifest=self.fixture(root)
            spec=json.loads(manifest.read_text());spec.pop('format')
            manifest.write_text(json.dumps(dict(format=1,modules=[dict(spec,id=0),dict(spec,id=17)])))
            result=prepare(manifest,root/'output')
            self.assertEqual((result['modules'],result['functions'],result['entries']),(2,2,4))
            for number,prefix in ((0,'g_ps2RecompiledFunctionTable'),(17,'g_ps2ArrangeFunctionTable')):
                code=(root/f'output/{number}/registration.cpp').read_text()
                self.assertIn(prefix+'Base',code)
                self.assertNotIn('g_ps2ArcadeFunctionTable',code)

    def test_generation_entry_anchor_uses_the_already_linked_body(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);manifest=self.fixture(root)
            mapping=root/'map.csv'
            with mapping.open('a') as stream:stream.write('anchor,0x730880,0x730888,8\n')
            receipt=root/'receipt.json';value=json.loads(receipt.read_text())
            value['functionMapSha256']=digest(mapping);receipt.write_text(json.dumps(value))
            spec=json.loads(manifest.read_text());spec['baseEntries']=['0x730880']
            manifest.write_text(json.dumps(spec))
            result=prepare(manifest,root/'output')
            self.assertEqual(result['functions'],1)
            self.assertNotIn('anchor_0x', (root/'output/registration.cpp').read_text())
            spec['baseEntries']=['0x730884'];manifest.write_text(json.dumps(spec))
            with self.assertRaisesRegex(ValueError,'already be linked'):prepare(manifest,root/'output')

    def test_mismatched_images_maps_and_unreviewed_root_linkage_rejected(self):
        for failure in ('image','map','root','report'):
            with self.subTest(failure=failure),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);manifest=self.fixture(root)
                if failure=='image':
                    p=root/'receipt.json';value=json.loads(p.read_text());value['elfSha256']='b'*64
                    p.write_text(json.dumps(value))
                elif failure=='report':
                    p=root/'receipt.json';value=json.loads(p.read_text());value['summary']['errors']=1
                    p.write_text(json.dumps(value))
                elif failure=='map':(root/'map.csv').write_text('changed')
                else:(root/'extra/fresh_0x730890.cpp').write_text('root_other();')
                with self.assertRaises(ValueError):prepare(manifest,root/'output')


if __name__=='__main__':unittest.main()
