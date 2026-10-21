from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from sdoj_logic_compare import compare, records, window


def row(logic,**changes):
    return dict(module=1,generation=2,logic=logic,batch=logic+7,thread=10,
                input0=8,input1=0,previous0=0,previous1=0,depth=1,
                command_bytes=40,command_sha256='ab'*32,valid=True,issue='none',
                t_us=logic*100,limit=8192)|changes


class LogicCompareTests(unittest.TestCase):
    def test_explicit_offsets_ignore_only_nonsemantic_wall_time_and_ids(self):
        left=window([row(i) for i in range(1,5)],module=1,generation=2,first=2,frames=2)
        right=window([row(i,generation=3,thread=20) for i in range(7,10)],
                     module=1,generation=3,first=7,frames=2)
        self.assertTrue(compare(left,right)['equal'])

    def test_every_semantic_difference_is_reported_not_re_aligned(self):
        for key,value in dict(input0=4,input1=2,previous0=16,previous1=8,depth=2,
                              command_bytes=76,command_sha256='cd'*32).items():
            with self.subTest(key=key):
                result=compare([row(1)],[row(20,**{key:value})])
                self.assertFalse(result['equal']); self.assertEqual(result['different_frames'],1)
                self.assertEqual(set(result['first_differences'][0]['differences']),{key})

    def test_missing_duplicate_bad_identity_and_rejected_records_fail(self):
        variants=([row(1)], [row(1),row(1)], [row(1),row(2,batch=99)],
                  [row(1),row(2,thread=20)], [row(1),row(2,t_us=1)],
                  [row(1),row(2,valid=False)], [row(1),row(2,depth=0)],
                  [row(1),row(2,command_bytes=3)])
        for rows in variants:
            with self.subTest(rows=rows):
                with self.assertRaises(ValueError):window(rows,module=1,generation=2,first=1,frames=2)

    def test_log_hex_fields_are_not_mistaken_for_decimal(self):
        text=('SDOJ_LOGIC module=1 generation=2 logic=1 batch=8 thread=10 '
              'input0=00000010 input1=00000000 previous0=00000000 previous1=00000000 '
              'depth=1 command_bytes=40 command_sha256='+('ab'*32)+
              ' valid=true issue=none t_us=100 limit=8192 no_guest_writes')
        self.assertEqual(records([text])[0]['input0'],16)
        with self.assertRaises(ValueError):records([text.replace('ab'*32,'bad')])
        with self.assertRaises(ValueError):records([text.replace(' depth=1','')])

    def test_no_empty_or_cross_module_success(self):
        with self.assertRaises(ValueError):compare([],[])
        with self.assertRaises(ValueError):compare([row(1)],[row(1,module=2)])

    def test_scope_does_not_claim_live_snapshot_is_decoded_replay_input(self):
        scope=compare([row(1)],[row(1)])['scope']
        self.assertIn('pre-copy live input snapshots',scope)
        self.assertIn('not decoded replay inputs',scope)

    def test_native_state_is_compared_when_present_and_not_silently_dropped(self):
        left=row(1,core_state=7,play_state=3,end_request=0)
        right=left|{'end_request':1}
        result=compare([left],[right])
        self.assertEqual(result['different_frames'],1)
        self.assertEqual(set(result['first_differences'][0]['differences']),{'end_request'})
        with self.assertRaises(ValueError):compare([left],[row(1)])
        with self.assertRaises(ValueError):window([left|{'core_state':-1}],module=1,generation=2,first=1,frames=1)


if __name__=='__main__':unittest.main()
