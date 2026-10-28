"""Keep explicit visual first-response decisions tied to exact original outputs."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/latency'))
from review_guwange_pixels import reviewed_response


def fixture(xenia=True):
    rows=[dict(seq=i,qpc=t,kind=0) for i,t in ((1,100),(2,200),(3,300))]
    if xenia:
        rows += [dict(seq=4,qpc=1000,kind=8,a=40,c=0x3000),
                 dict(seq=5,qpc=51000,kind=8,a=50,c=0x4000)]
        keys=[(40,0x3000),(50,0x4000)]
        owned,response=4,5
    else:
        rows += [dict(seq=4,qpc=800,kind=6,a=0x3000),
                 dict(seq=5,qpc=900,kind=7,a=4),
                 dict(seq=6,qpc=1000,kind=8,a=5,e=90),
                 dict(seq=7,qpc=50800,kind=6,a=0x4000),
                 dict(seq=8,qpc=50900,kind=7,a=7),
                 dict(seq=9,qpc=51000,kind=8,a=8,e=91)]
        keys=[(90,0x3000),(91,0x4000)]
        owned,response=6,9
    trace=dict(pid=1,frequency=1000000,complete=True,records=rows)
    chain=dict(samples=[dict(command=101,points=dict(source=1,input_api_return=2,
               game_input=3,present_begin=owned))])
    capture=dict(header=dict(pid=1,frequency=1000000),footer=dict(overflow=0),
        frames=[dict(index=i,source_frame=100+i,source_event=0,source_packet=0,present_begin=i*1000)
                for i in range(8)])
    for index,key,qpc in ((3,keys[0],1000),(5,keys[1],51000)):
        capture['frames'][index].update(source_event=key[0],source_packet=key[1],present_begin=qpc)
    tracker=dict(accepted=[],examined=[dict(command=101,owned_index=3,frame_indexes=list(range(7)),
                 x=[10,10,10,10,10,50,70])])
    return trace,chain,capture,[tracker,copy.deepcopy(tracker)]


class GuwangePixelResponseTests(unittest.TestCase):
    def review(self,args,xenia=True):
        return reviewed_response(*args,xenia=xenia,owned_index=3)

    def test_xenia_retains_two_output_delay_not_nearest_present(self):
        result=self.review(fixture())
        self.assertEqual(result['owned_frame_offset'],2)
        self.assertEqual(result['game_input_to_response_present_ms'],50.7)
        self.assertIsNone(result['p95_ms'])
        self.assertIn('not an automated NCC pass',result['method'])

    def test_native_response_uses_its_own_packet_and_paint(self):
        result=self.review(fixture(False),False)
        self.assertEqual(result['response_present_seq'],9)
        self.assertEqual(result['owned_output_to_response_present_ms'],50)

    def test_different_process_or_incomplete_trace_rejected(self):
        for mutate in (lambda a:a[0].update(complete=False),
                       lambda a:a[2]['header'].update(pid=2)):
            args=fixture();mutate(args)
            with self.assertRaisesRegex(ValueError,'same-process'):
                self.review(args)

    def test_wrong_starting_packet_rejected(self):
        args=fixture();args[2]['frames'][3]['source_packet']+=4
        with self.assertRaisesRegex(ValueError,'core-owned'):
            self.review(args)

    def test_wrong_response_identity_or_clock_rejected(self):
        for mutate in (lambda a:a[2]['frames'][5].update(source_packet=0),
                       lambda a:a[2]['frames'][5].update(present_begin=60000)):
            args=fixture();mutate(args)
            with self.assertRaises(ValueError):
                self.review(args)

    def test_prior_motion_is_not_a_stationary_baseline(self):
        args=fixture();args[3][0]['examined'][0]['x'][4]=12
        with self.assertRaisesRegex(ValueError,'Pre-response'):
            self.review(args)

    def test_missing_or_skipped_output_continuation_rejected(self):
        for mutate in (lambda a:a[2]['frames'].pop(),
                       lambda a:a[2]['frames'][4].update(source_frame=200)):
            args=fixture();mutate(args)
            with self.assertRaises(ValueError):
                self.review(args)


if __name__=='__main__':
    unittest.main()
