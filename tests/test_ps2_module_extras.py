from pathlib import Path
import json
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from project import digest
from ps2_module_extras import prepare, fixed_branch_gaps


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

    def test_reviewed_return_continuation_is_retained_only_with_identical_epilogue(self):
        for failure in (None,'stack32','opcode','generator','entry'):
            with self.subTest(failure=failure),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);manifest=self.fixture(root)
                with (root/'base/register_functions.cpp').open('a') as stream:
                    stream.write('g_ps2RecompiledFunctionTable[5] = old_return; // 0x730894\n')
                tail=('// 0x730894: 0xdfbf0000 ld ra,0(sp)\n'
                      '// 0x730898: 0x03e00008 jr ra\n'
                      '// 0x73089c: 0x27bd0010 addiu sp,sp,16\n')
                if failure=='stack32':tail=tail.replace('0x27bd0010','0x27bd0020')
                (root/'base/old_return.cpp').write_text(tail)
                (root/'extra/fresh_0x730890.cpp').write_text(
                    tail.replace('0x27bd0010','0x27bd0020') if failure=='opcode' else tail)
                for name in ('receipt.json','base-receipt.json'):
                    path=root/name;receipt=json.loads(path.read_text())
                    receipt['toolSha256']='b' if failure=='generator' and name=='receipt.json' else 'a'
                    path.write_text(json.dumps(receipt))
                spec=json.loads(manifest.read_text())
                spec['retainedReturnSlots']=['0x730890' if failure=='entry' else '0x730894']
                manifest.write_text(json.dumps(spec))
                if failure and failure!='stack32':
                    with self.assertRaisesRegex(ValueError,'Retained return'):prepare(manifest,root/'output')
                else:
                    result=prepare(manifest,root/'output')
                    self.assertEqual((result['functions'],result['entries']),(1,1))
                    self.assertNotIn('{0x730894u,',(root/'output/registration.cpp').read_text())

    def test_boot_arrange_and_stages_use_their_own_tables(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);manifest=self.fixture(root)
            spec=json.loads(manifest.read_text());spec.pop('format')
            manifest.write_text(json.dumps(dict(format=1,modules=[dict(spec,id=n) for n in (0,17,1,9)])))
            result=prepare(manifest,root/'output')
            self.assertEqual((result['modules'],result['functions'],result['entries']),(4,4,8))
            for number,prefix in ((0,'g_ps2RecompiledFunctionTable'),(17,'g_ps2ArrangeFunctionTable'),
                                  (1,'g_ps2Stage01FunctionTable'),(9,'g_ps2Stage09FunctionTable')):
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

    def test_fixed_branch_uses_active_mode_and_reports_missing_function_before_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);manifest=self.fixture(root)
            module=json.loads(manifest.read_text());module.pop('format')
            alternate=root/'arrange';alternate.mkdir()
            (alternate/'register_functions.cpp').write_text(
                (root/'base/register_functions.cpp').read_text()+
                'g_ps2RecompiledFunctionTable[12] = alternate; // 0x7308b0\n')
            (root/'extra/fresh_0x730890.cpp').write_text(
                'runtime->dispatchGuestBranch(rdram, ctx, 0x7308b0u, 0x730890u, 0x730898u);')
            spec=dict(format=1,checkLinks=True,modules=[dict(module,id=0),dict(module,id=16),
                      dict(module,id=17,base=str(alternate))])
            gaps=fixed_branch_gaps(spec)
            self.assertEqual([(g['module'],g['target']) for g in gaps],[(16,0x7308b0)])
            manifest.write_text(json.dumps(spec))
            with self.assertRaisesRegex(ValueError,'module 16:0x7308b0'):
                prepare(manifest,root/'output')
            self.assertFalse((root/'output').exists())

    def test_stage_fixed_calls_cannot_use_the_other_mode(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);manifest=self.fixture(root)
            module=json.loads(manifest.read_text());module.pop('format')
            alternate=root/'arrange';alternate.mkdir()
            (alternate/'register_functions.cpp').write_text(
                (root/'base/register_functions.cpp').read_text()+
                'g_ps2RecompiledFunctionTable[12] = alternate; // 0x7308b0\n')
            (root/'extra/fresh_0x730890.cpp').write_text(
                'runtime->dispatchGuestBranch(rdram, ctx, 0x7308b0u, 0x730890u, 0x730898u);')
            spec=dict(modules=[dict(module,id=n,base=str(alternate) if n==17 else module['base'])
                               for n in (0,16,17,1,9)],
                      linkedTables=[str(alternate/'register_functions.cpp')])
            self.assertEqual([(g['module'],g['target']) for g in fixed_branch_gaps(spec)],[(9,0x7308b0)])

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
