import sys
import re
from pathlib import Path
import unittest
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from ps2_input_order import CALLS, FILES, POLICY, rewrite, prepare, verify


def fixture():
    source = ''
    for pc, target, _ in CALLS:
        source += f'''    // 0x{pc:x}: 0x{0x0c000000 | (target >> 2):x}  jal         func_{target:X}
    SET_GPR_U32(ctx, 31, 0x{pc+8:X}u);
    // Deliberately retain a visible side effect in the delay slot.
    WRITE32(0x358c38u, 0u);
    ctx->pc = 0x{target:X}u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x{target:X}u, 0x{pc:X}u, 0x{pc+8:X}u, kind, "JAL")) {{
        return;
    }}
    ctx->pc = 0x{pc+8:X}u;
label_{pc+8:x}:
'''
    return source


class Ps2InputOrder(unittest.TestCase):
    def test_targets_move_wait_before_both_pad_routines(self):
        result = rewrite(fixture())
        self.assertEqual([int(v,16) for v in re.findall(
            r'dispatchGuestBranch\(rdram, ctx, 0x([0-9A-F]+)u', result)],
            [0x11d480, 0x11e840, 0x1210a0])

    def test_resume_source_and_delay_effects_are_unchanged(self):
        result = rewrite(fixture())
        for pc, _, _ in CALLS:
            self.assertIn(f'SET_GPR_U32(ctx, 31, 0x{pc+8:X}u);', result)
            self.assertIn(f'0x{pc:X}u, 0x{pc+8:X}u, kind, "JAL"', result)
            self.assertIn(f'label_{pc+8:x}:', result)
        self.assertEqual(result.count('WRITE32(0x358c38u, 0u);'), 3)
        self.assertEqual(result.count('        return;'), 3)

    def test_changed_guest_or_generator_is_rejected(self):
        for old, new in [('func_11E840','func_11E844'),
                         ('ctx, 0x1210A0u,','ctx, 0x1210A4u,'),
                         ('0x1007CCu, kind','0x1007D0u, kind')]:
            with self.subTest(old=old), self.assertRaises(ValueError):
                rewrite(fixture().replace(old,new))

    def test_candidate_cannot_be_applied_twice(self):
        with self.assertRaises(ValueError):
            rewrite(rewrite(fixture()))

    def test_receipt_binds_both_compiled_bodies_and_their_policy(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);generated=root/'original';generated.mkdir();output=root/'compiled'
            for name in FILES:(generated/name).write_text(fixture(),encoding='utf8')
            prepare(generated,output)
            self.assertEqual(verify(generated,output)['policy'],POLICY)
            self.assertEqual(len(verify(generated,output)['files']),2)
            (output/FILES[1]).write_text('changed compiled body',encoding='utf8')
            with self.assertRaises(ValueError):verify(generated,output)
            self.assertEqual((output/FILES[1]).read_text(),'changed compiled body')

    def test_receipt_rejects_changed_original_and_stale_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);generated=root/'original';generated.mkdir();output=root/'compiled'
            for name in FILES:(generated/name).write_text(fixture(),encoding='utf8')
            prepare(generated,output)
            (output/'identity.json').write_text('{}',encoding='utf8')
            with self.assertRaises(ValueError):verify(generated,output)

    def test_compacted_frame_entries_patch_the_shared_body_once(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);generated=root/'original';generated.mkdir();output=root/'compiled'
            (generated/FILES[0]).write_text(fixture(),encoding='utf8')
            name=Path(FILES[0]).stem
            (generated/'register_functions.cpp').write_text(
                f'g_ps2RecompiledFunctionTable[0] = {name}; // 0x1004d0\n'
                f'g_ps2RecompiledFunctionTable[1] = {name}; // 0x1007b0\n')
            prepare(generated,output)
            self.assertEqual(len(verify(generated,output)['files']),1)
            prepare(generated,output)
            (generated/FILES[0]).write_text('changed original body',encoding='utf8')
            with self.assertRaises(ValueError):verify(generated,output)


if __name__ == '__main__':
    unittest.main()
