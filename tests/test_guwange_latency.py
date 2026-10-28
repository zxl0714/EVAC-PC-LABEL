"""Original arcade-port and queue ownership, without any game data or window."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from guwange_latency import ADDRESSES, analyze, queues


def fixture(early_core=False):
    rows = []
    def event(kind, *values, thread=10):
        seq = len(rows) + 1
        rows.append(dict(seq=seq,qpc=seq*10,thread=thread,kind=kind,
            **dict(zip(('a','b','c','d','e','f'),(*values,0,0,0,0,0,0)))))
        return seq
    def site(identifier,*values,thread=10):
        return event(10,identifier,ADDRESSES[identifier],*values,thread=thread)
    source = event(14,101,8,1)
    if early_core:
        begin = event(3,0)
        event(15,source,0,source,8)
        event(4,begin,0,0,source,8)
        site(45,0xef,0x7000,0,0x88020048)
    site(1)
    begin = event(3,0)
    event(15,source,0,source,8)
    event(4,begin,0,0,source,8)
    site(2)
    site(5)
    site(30,0x88020000,4,0,0x821cca7c)
    site(44,0x10d00011,0x7000,0,0x88020048)
    site(45,0xef,0x7000,0,0x88020048)
    site(31,0,0,0x10000000,0)
    site(6)
    site(7,0x821f7620,0x4000,0x8000,0)
    site(21,0x8000,0x9000,0,0xa000)
    site(8)
    site(9,0x821f7720,0x4000,0x8000,0)
    site(22,(1<<32)|0x8000,0x9000,8,0xa000)
    site(10)
    site(11,0x9000,0,0xa000,1,thread=20)
    site(12,0x8000,0x9000,1,0,thread=20)
    site(13,0x8000,0,0x8000,0x9000,thread=20)
    site(14,1,0x8000,0,0x9000,thread=20)
    site(11,0x9000,8,0xa000,1,thread=20)
    site(12,(1<<32)|0x8000,0x9000,1,0,thread=20)
    site(15,1,0x4000,0x9000,0,thread=20)
    for identifier in (16,17,18,19):
        site(identifier,thread=20)
    event(13,0x3000,thread=20)
    site(20,thread=20)
    swap = event(6,0x3000,thread=40)
    ready = event(7,swap,2,0,0,1,thread=40)
    present = event(8,ready,0,0,1,thread=40)
    event(9,present,ready,0,thread=40)
    site(1)
    return dict(frequency=1000000,pid=1,capacity=1000,committed=len(rows),reserved=len(rows),
        complete=True,incomplete=0,overflow=0,records=rows)


class GuwangeLatencyTests(unittest.TestCase):
    def test_original_port_same_list_slots_and_swap(self):
        result = analyze(fixture())
        self.assertEqual(result['paired_sources'],1)
        self.assertEqual(result['samples'][0]['arcade_port_value'],0xef)
        self.assertEqual(result['samples'][0]['buffer'],0x8000)
        self.assertIn('first_response_pixel',result['unmeasured'])

    def test_first_incomplete_core_not_replaced_by_later_full_cycle(self):
        self.assertEqual(analyze(fixture(True))['paired_sources'],0)

    def test_direction_must_be_read_by_original_arcade_load(self):
        trace = fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==45)['c']=0xf7
        result = analyze(trace)
        self.assertEqual(result['paired_sources'],0)
        self.assertIn('direction_not_read_by_arcade_core',result['rejected_cycles'])

    def test_exact_port_address_and_context_required(self):
        trace = fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==44)['c']+=1
        self.assertEqual(analyze(trace)['paired_sources'],0)

    def test_same_payload_at_different_slot_is_not_ownership(self):
        trace = fixture()
        entries = [r for r in trace['records'] if r['kind']==10 and r['a']==11]
        entries[-1]['d']=16
        self.assertEqual(analyze(trace)['paired_sources'],0)

    def test_wrong_draw_list_not_accepted(self):
        trace = fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==13)['c']+=4
        self.assertEqual(analyze(trace)['paired_sources'],0)

    def test_present_from_another_physical_packet_rejected(self):
        trace = fixture()
        next(r for r in trace['records'] if r['kind']==13)['a']+=4
        with self.assertRaisesRegex(ValueError,'ownership'):
            analyze(trace)

    def test_incomplete_or_changed_original_site_rejected(self):
        trace = fixture()
        trace['complete']=False
        with self.assertRaisesRegex(ValueError,'Complete'):
            analyze(trace)
        trace = fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==45)['b']+=4
        with self.assertRaisesRegex(ValueError,'pinned original'):
            analyze(trace)


if __name__ == '__main__':
    unittest.main()
