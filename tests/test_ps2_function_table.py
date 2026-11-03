import sys
from pathlib import Path
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from ps2_function_table import prepare, render, verify


def source(entries=((0, 'A'), (1, 'A'), (3, 'B'), (7, 'A'))):
    return ('#include "ps2_runtime.h"\n'
            'extern const uint32_t g_ps2RecompiledFunctionTableBase = 0x100000u;\n'
            'extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0x100020u;\n'
            'extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = 8u;\n'
            'PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[8u] = {};\n'
            'namespace {\nstruct GeneratedFunctionTableInitializer {\n'
            '    GeneratedFunctionTableInitializer() {\n' + ''.join(
                f'        g_ps2RecompiledFunctionTable[{slot}] = {name}; // 0x{0x100000+slot*4:x}\n'
                for slot, name in entries) +
            '    }\n};\nstatic const GeneratedFunctionTableInitializer '
            'g_generatedFunctionTableInitializer;\n}\n')


class FunctionTableTests(unittest.TestCase):
    def test_runs_keep_noncontiguous_holes_and_repeated_owners(self):
        text, report = render(source())
        self.assertEqual((report['entries'], report['runs'], report['slots']), (4, 3, 8))
        self.assertIn('{0u,2u,A}', text)
        self.assertIn('{3u,1u,B}', text)
        self.assertIn('{7u,1u,A}', text)
        self.assertIn('g_ps2RecompiledFunctionTable[8u] = {};', text)

    def test_empty_mapping(self):
        text, report = render(source(()))
        self.assertEqual(report['entries'], 0)
        self.assertIn('g_ps2RecompiledFunctionTableSlotCount, {});', text)

    def test_zero_extent_uses_the_generators_one_slot_placeholder(self):
        original = source(()).replace('End = 0x100020', 'End = 0x100000')
        original = original.replace('SlotCount = 8u', 'SlotCount = 0u').replace('Table[8u]', 'Table[1u]')
        text, report = render(original)
        self.assertEqual((report['slots'], report['entries']), (0, 0))
        self.assertIn('Table[1u] = {};', text)

    def test_rejects_duplicate_unordered_and_out_of_range_slots(self):
        for entries in (((1, 'A'), (1, 'B')), ((2, 'A'), (1, 'A')), ((8, 'A'),)):
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                render(source(entries))

    def test_rejects_inconsistent_pc_extent_and_unknown_code(self):
        for text in (source().replace('// 0x100004', '// 0x100008'),
                     source().replace('End = 0x100020', 'End = 0x100024'),
                     source().replace('    }\n};', '        unknown();\n    }\n};')):
            with self.assertRaises(ValueError): render(text)

    def test_preserves_input_and_detects_changed_build_view(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); original = root / 'original.cpp'; original.write_text(source())
            output = prepare(original, root / 'view')
            self.assertEqual(original.read_text(), source())
            self.assertEqual(verify(original, root / 'view')['entries'], 4)
            output.write_text(output.read_text().replace('{0u,2u,A}', '{0u,2u,B}'))
            with self.assertRaises(ValueError): verify(original, root / 'view')

    def test_compilation_batch_uses_the_compact_view_and_keeps_its_timestamp(self):
        from ps2_translation_units import make_units
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); generated = root / 'generated'; generated.mkdir()
            original = generated / 'register_functions.cpp'; original.write_text(source())
            (generated / 'ps2_recompiled_functions.h').write_text('// fixture')
            (generated / 'body.cpp').write_text('// unchanged body')
            make_units(generated, root / 'units', table_root=root / 'tables')
            unit = root / 'units/batch_0000.cpp'; text = unit.read_text()
            self.assertNotIn(original.as_posix(), text)
            self.assertIn((generated / 'body.cpp').as_posix(), text)
            self.assertEqual(original.read_text(), source())
            stamp = unit.stat().st_mtime_ns
            make_units(generated, root / 'units', table_root=root / 'tables')
            self.assertEqual(unit.stat().st_mtime_ns, stamp)


if __name__ == '__main__': unittest.main()
