"""G2 latency requires original input/update ownership, not nearest Present."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from g2_latency import ADDRESSES, analyze


def fixture():
    rows = []
    def event(kind, *values, qpc=None, thread=10):
        seq = len(rows)+1
        r = dict(seq=seq, qpc=qpc if qpc is not None else seq*10, thread=thread, kind=kind)
        r.update(dict(zip(('a','b','c','d','e','f'), (*values,0,0,0,0,0,0))))
        rows.append(r)
        return seq
    def site(i, *regs):
        return event(10,i,ADDRESSES[i],*regs)
    source = event(14,101,8,1)
    site(1)
    begin = event(3,0)
    event(15,source,0,source,8)
    event(4,begin,0,0,source,8)
    site(2)
    site(4,0x88010000,0x10000,0,0x880202A8)
    site(11,8,0,0,0x82091690)
    site(12)
    site(13)
    site(14)
    site(5,1,0x10000)
    site(16)
    site(17)
    site(15)
    site(8)
    site(9)
    site(6)
    event(13,0x300)
    site(7,0x0c000000)
    site(1)
    swap = event(6,0x300,thread=20)
    ready = event(7,swap,2,0,0,1,thread=20)
    present = event(8,ready,0,0,1,thread=20)
    event(9,present,ready,0,thread=20)
    return dict(frequency=1000000,pid=1,capacity=1000,committed=len(rows),reserved=len(rows),
                overflow=0,incomplete=0,complete=True,records=rows)


class G2LatencyTests(unittest.TestCase):
    def test_owned_output_can_arrive_after_next_cycle(self):
        r = analyze(fixture(),101,101)
        self.assertEqual(r['paired_sources'],1)
        self.assertEqual(r['samples'][0]['mapped_buttons'],8)
        self.assertGreater(r['samples'][0]['times']['game_input_to_present_begin_us'],0)

    def test_changed_direction_must_reach_original_core(self):
        trace = fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==11)['c']=4
        r=analyze(trace,101,101)
        self.assertEqual(r['paired_sources'],0)
        self.assertEqual(r['rejected_cycles']['direction_not_consumed_by_core'],1)

    def test_reject_wrong_virtual_target(self):
        trace = fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==4)['f']=0x880202AC
        r=analyze(trace,101,101)
        self.assertEqual(r['paired_sources'],0)
        self.assertEqual(r['rejected_cycles']['wrong_core_entry'],1)

    def test_nearby_output_is_not_owned_output(self):
        trace = fixture()
        next(r for r in trace['records'] if r['kind']==13)['a']=0x400
        with self.assertRaisesRegex(ValueError,'output links'):
            analyze(trace,101,101)

    def test_original_sites_must_remain_synchronous(self):
        trace = fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==13)['thread']=11
        r=analyze(trace,101,101)
        self.assertEqual(r['paired_sources'],0)
        self.assertEqual(r['rejected_cycles']['not_synchronous_original_cycle'],1)

    def test_incomplete_trace_rejected(self):
        trace=fixture(); trace['complete']=False
        with self.assertRaisesRegex(ValueError,'Complete'):
            analyze(trace)


if __name__=='__main__':
    unittest.main()
