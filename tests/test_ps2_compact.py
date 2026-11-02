from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from ps2_compact import analyze, has_delay_slot, protected_starts, verify_generated


def row(name, start, end):
    return dict(Name=name, Start=hex(start), End=hex(end), Size=str(end-start))


class CompactTests(unittest.TestCase):
    def test_interior_entries_share_a_body_without_losing_addresses(self):
        rows = [row('FUN_a', 0x1000, 0x1040), row('entry_a', 0x1010, 0x1020),
                row('entry_b', 0x1020, 0x1040), row('entry_standalone', 0x1040, 0x1050)]
        kept, report = analyze(rows, lambda pc: 0)
        self.assertEqual([r['Name'] for r in kept], ['FUN_a', 'entry_standalone'])
        self.assertEqual([(r['entry'], r['owner']) for r in report['aliases']],
                         [(0x1010, 0x1000), (0x1020, 0x1000)])
        self.assertEqual(analyze(list(reversed(rows)), lambda pc: 0), (kept, report))

    def test_delay_slot_and_native_entries_keep_independent_bodies(self):
        rows = [row('FUN_a', 0x1000, 0x1040), row('entry_a', 0x1010, 0x1020)]
        for word, protected, reason in ((lambda pc: 0x03e00008, (), 'delay-slot-entry'),
                (lambda pc: 0, (0x1010,), 'protected'), (lambda pc: 0, (0x1000,), 'native-owner')):
            kept, report = analyze(rows, word, protected)
            self.assertEqual(kept, rows)
            self.assertEqual(report['excluded'], {reason: 1})

    def test_crossing_ranges_and_unmapped_code_are_not_folded(self):
        kept, report = analyze([row('FUN_a', 0x1000, 0x1020),
            row('entry_a', 0x1010, 0x1030)], lambda pc: 0)
        self.assertEqual(report['sharedEntries'], 0)
        self.assertEqual(len(kept), 2)
        _, report = analyze([row('FUN_a', 0x1000, 0x1040),
            row('entry_a', 0x1010, 0x1020)], lambda pc: None)
        self.assertEqual(report['excluded'], {'unmapped': 1})

    def test_overlays_are_analyzed_independently(self):
        for name in ('module_a', 'module_b'):
            _, report = analyze([row(name, 0x1000, 0x1040),
                row('entry_'+name, 0x1010, 0x1020)], lambda pc: 0)
            self.assertEqual(report['aliases'][0]['name'], 'entry_'+name)

    def test_invalid_intervals_and_duplicate_pcs_are_rejected(self):
        for rows in ([row('a', 0x1000, 0x1002)], [row('a', 0x1000, 0x1000)],
                     [row('a', 0x1000, 0x1010), row('b', 0x1000, 0x1010)]):
            with self.assertRaises(ValueError):
                analyze(rows, lambda pc: 0)

    def test_refined_end_is_authoritative_over_ghidra_body_size(self):
        owner = row('FUN_a', 0x1000, 0x1040)
        owner['Size'] = '80'
        _, report = analyze([owner,row('entry_a',0x1010,0x1020)],lambda pc:0)
        self.assertEqual(report['outputInstructionBytes'],64)

    def test_delay_slot_decoding_and_protected_selectors(self):
        for opcode in (0x08040000, 0x0c040000, 0x03e00008, 0x0320f809,
                       0x10000004, 0x50000004, 0x45010004):
            self.assertTrue(has_delay_slot(opcode), hex(opcode))
        self.assertFalse(has_delay_slot(0x24020001))
        self.assertEqual(protected_starts(dict(stubs=['native@0x1000'],
            skip=['0x2000'], compact_keep_entries=['0x3000'])), {0x1000, 0x2000, 0x3000})

    def test_actual_elf_owner_must_have_a_real_resume_label(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            source=root/'elf_owner.cpp'
            source.write_text('// Address: 0x1000 - 0x1040\n'
                'case 0x1020u: goto label_1020;\nlabel_1020:\n')
            report=dict(aliases=[dict(entry=0x1020,owner=0x1010)])
            table={0x1020:'elf_owner',0x1010:'map_owner'}
            self.assertEqual(verify_generated(report,root,table)['elfOwnerRemappings'],1)
            source.write_text('// Address: 0x1000 - 0x1040\n')
            with self.assertRaisesRegex(ValueError,'cannot resume'):
                verify_generated(report,root,table)
            with self.assertRaisesRegex(ValueError,'lost callable'):
                verify_generated(report,root,{})


if __name__ == '__main__':
    unittest.main()
