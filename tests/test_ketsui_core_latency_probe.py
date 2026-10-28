"""Core discovery rejects bad ownership and keeps menu inputs out of results."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/latency'))
from audit_ketsui_core import ordered
from inspect_ketsui_core_probe import inspect


def fixture():
    rows=[]
    def event(kind,*values,qpc=None,thread=10):
        seq=len(rows)+1
        rows.append(dict(seq=seq,qpc=qpc or seq*10,thread=thread,kind=kind,
            **dict(zip(('a','b','c','d','e','f'),(*values,0,0,0,0,0,0)))))
        return seq
    source=event(14,101,8,1)
    begin=event(10,100,0x6610,1,2,3,0x10029629)
    apply=event(10,102,0x5c0a0,begin,0,(3<<32)|8,0x8000)
    event(10,103,0x5c0a0,apply,begin,0)
    event(10,101,0x6610,begin,0,0x9000,0x100000000)
    event(14,102,4,1)
    return dict(pid=1,frequency=1000000,capacity=1000,committed=len(rows),reserved=len(rows),
        complete=True,incomplete=0,overflow=0,records=rows)


class KetsuiCoreProbeTests(unittest.TestCase):
    def test_reports_delivery_not_input_to_first_image(self):
        result=inspect(fixture())
        self.assertEqual(result['original_callback_frames'],1)
        self.assertEqual(result['deliveries_in_owned_callback'],1)
        self.assertIn('not input-to-first-image latency',result['scope'])
        self.assertEqual(result['sources_outside_core_interval'],[101,102])

    def test_cross_thread_completion_rejected(self):
        trace=fixture();trace['records'][4]['thread']=20
        with self.assertRaisesRegex(ValueError,'completion identity'):
            inspect(trace)

    def test_cross_thread_delivery_rejected(self):
        trace=fixture();trace['records'][2]['thread']=20
        with self.assertRaisesRegex(ValueError,'outside its original callback'):
            inspect(trace)

    def test_wrong_site_or_incomplete_rejected(self):
        trace=fixture();trace['records'][2]['b']+=1
        with self.assertRaises(ValueError):
            inspect(trace)
        trace=fixture();trace['complete']=False
        with self.assertRaises(ValueError):
            inspect(trace)

    def test_instruction_order_is_not_presence_only(self):
        code=[dict(address=1,asm='mov r15, rsi'),dict(address=2,asm='call 0x2f8')]
        self.assertEqual(len(ordered(code,['mov r15','call'])),2)
        with self.assertRaises(ValueError):
            ordered(code,['call','mov r15'])


if __name__=='__main__':
    unittest.main()
