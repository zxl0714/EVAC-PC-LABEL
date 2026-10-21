from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import sdoj_prefix_fixture as fixture


class NativePrefixFixtureTests(unittest.TestCase):
    def test_extracts_one_complete_function_not_neighbor_declarations(self):
        source=('DEFINE_REX_FUNC(one) {\n  value();\n}\nextern void neighbor();\n'
                'DEFINE_REX_FUNC(two) {\n  other();\n}\n')
        self.assertEqual(fixture.function(source,'one'),'DEFINE_REX_FUNC(one) {\n  value();\n}')
        with self.assertRaises(ValueError): fixture.function(source,'missing')

    def test_local_fixture_keeps_prefix_and_native_cleanup_but_not_parser(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            generated=root/'generated/sdoj/CA022100'; generated.mkdir(parents=True)
            source=('DEFINE_REX_FUNC(sub_88044960) {\n'
                    ' if (SdojDelayInputStart(ctx, base)) goto loc_880451D4;\n'
                    ' native_input();\nloc_880451D4:\n parser_must_not_run();\n'
                    'loc_88045DC0:\n native_cleanup();\n}\n')
            source+='\n'.join(f'DEFINE_REX_FUNC({name}) {{\n save_restore();\n}}\n'
                              for name in ('__savegprlr_20','__restgprlr_20'))
            (generated/'sdoj_recomp.0.cpp').write_text(source,encoding='utf-8')
            # Helpers may be assigned to a different generated chunk.
            (generated/'sdoj_recomp.7.cpp').write_text('\n'.join(
                f'DEFINE_REX_FUNC({name}) {{\n float_restore();\n}}\n'
                for name in ('__savefpr_28','__restfpr_28')),encoding='utf-8')
            with patch.object(fixture,'ROOT',root):
                output=root/'.local/test/prefix.inc'
                report=fixture.prepare('CA022100',output)
                text=output.read_text(encoding='utf-8')
                self.assertIn('native_input()',text)
                self.assertIn('native_cleanup()',text)
                self.assertNotIn('parser_must_not_run()',text)
                self.assertIn('DEFINE_REX_FUNC(NativePrefix)',text)
                self.assertEqual(text.count('SdojDelayInputDone(ctx, base)'),1)
                self.assertEqual(report['epilogue'],'88045DC0')
                with self.assertRaises(ValueError):
                    fixture.prepare('CA022100',root/'source/leaked.inc')


if __name__=='__main__': unittest.main()
