"""SDOJ requires actual buffer publications and original queue-slot ownership."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from sdoj_xenia_latency import BLOCK, SITES, analyze


def fixture(early_unpaired_copy=False):
    rows=[]
    def event(kind,*values,thread=10):
        seq=len(rows)+1
        r=dict(seq=seq,qpc=seq*10,thread=thread,kind=kind)
        r.update(dict(zip(('a','b','c','d','e','f'),(*values,0,0,0,0,0,0))))
        rows.append(r)
        return seq
    def site(identifier,*registers,thread=10):
        return event(10,identifier,SITES[identifier],*registers,thread=thread)
    source=event(14,101,8,1)
    site(1)
    begin=event(3,0)
    event(15,source,0,source,8)
    event(4,begin,0,0,source,8)
    site(2)
    if early_unpaired_copy:
        site(4,8,0,BLOCK,BLOCK,thread=20)
    site(19)
    site(23,8,0,BLOCK)
    site(20)
    site(4,8,0,BLOCK,BLOCK,thread=20)
    site(5,0x10000,0,0,0,thread=20)
    site(6,0x10040,0,0,0,thread=20)
    site(7,1,0x10000,0,0)
    site(8,0x10000,0x60000,0,0)
    site(9)
    site(10,0x1000,0,0x2000,0x60000)
    site(12,0x1000,0x2000,0x7000,1,thread=30)
    site(13,0x1000,0,0x60000,0x7000,thread=30)
    site(11,0x1000,8,0x2000,1<<32)
    site(12,0x1000,0x2008,0x7000,1,thread=30)
    site(13,0x1000,1,0,0x7000,thread=30)
    site(15,thread=30)
    site(16,thread=30)
    site(17,thread=30)
    event(13,0x300,thread=30)
    site(18,thread=30)
    swap=event(6,0x300,thread=40)
    ready=event(7,swap,2,0,0,1,thread=40)
    present=event(8,ready,0,0,1,thread=40)
    event(9,present,ready,0,thread=40)
    return dict(frequency=1000000,pid=1,capacity=1000,committed=len(rows),reserved=len(rows),
                overflow=0,incomplete=0,complete=True,records=rows)


class SdojXeniaLatencyTests(unittest.TestCase):
    def test_owned_cross_thread_output(self):
        report=analyze(fixture(),101,101)
        self.assertEqual(report['paired_sources'],1)
        self.assertEqual(report['samples'][0]['buffer'],0x10000)
        self.assertEqual(report['samples'][0]['draw_entries'][0]['argument'],0x60000)
        self.assertIn('first_response_pixel',report['unmeasured'])

    def test_incomplete_first_consumption_cannot_be_replaced(self):
        self.assertEqual(analyze(fixture(True),101,101)['paired_sources'],0)

    def test_publication_must_match_completed_buffer(self):
        trace=fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==7)['d']=0x20000
        self.assertEqual(analyze(trace,101,101)['paired_sources'],0)

    def test_dequeued_payload_must_match_the_same_slot(self):
        trace=fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==13)['e']=0x60004
        self.assertEqual(analyze(trace,101,101)['paired_sources'],0)

    def test_nearby_present_cannot_replace_owned_packet(self):
        trace=fixture()
        next(r for r in trace['records'] if r['kind']==13)['a']=0x400
        with self.assertRaisesRegex(ValueError,'output/site identity'):
            analyze(trace,101,101)

    def test_original_instruction_and_complete_capture_required(self):
        trace=fixture()
        trace['complete']=False
        with self.assertRaisesRegex(ValueError,'Complete'):
            analyze(trace)
        trace=fixture()
        next(r for r in trace['records'] if r['kind']==10)['b']+=4
        with self.assertRaisesRegex(ValueError,'original SDOJ instruction'):
            analyze(trace)


if __name__=='__main__':
    unittest.main()
