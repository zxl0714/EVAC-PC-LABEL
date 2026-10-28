"""Original Daioujou tail-caller, earliest-report and response identity gates."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'tools/latency'),str(ROOT/'tests')]
from daioujou_core_latency import analyze
from prepare_daioujou_probe import header
from review_daioujou_pixels import response_indices
from review_shad_daioujou_pixels import response_indices as shad_response
from prepare_shad_daioujou_probe import transform
from test_ketsui_core_latency import fixture


def doi_fixture(**kwargs):
    trace=fixture(**kwargs)
    for r in trace['records']:
        if r['kind']==10:
            r['b']={100:0x6700,101:0x6700,102:0x61250,103:0x61250}[r['a']]
            if r['a']==100:r['f']=0x1001b55e
    return trace


def pair(trace):return analyze(trace,0x10000000,{0x27c04,0x1b55e})


class DaioujouLatencyTests(unittest.TestCase):
    def test_original_tail_parent_return_is_accepted(self):
        result=pair(doi_fixture())
        self.assertEqual(result['paired_sources'],1)
        self.assertEqual(result['samples'][0]['caller_offset'],0x1b55e)
        self.assertIn('actual_emulated_CPU_port_load',result['unmeasured'])

    def test_original_direct_return_also_accepted(self):
        trace=doi_fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==100)['f']=0x10027c04
        self.assertEqual(pair(trace)['paired_sources'],1)

    def test_ketsui_caller_cannot_be_reused(self):
        trace=doi_fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==100)['f']=0x10029629
        with self.assertRaisesRegex(ValueError,'tail-parent'):pair(trace)

    def test_failed_earliest_callback_not_replaced(self):
        self.assertEqual(pair(doi_fixture(bad_first=True))['paired_sources'],0)

    def test_last_report_not_earlier_pressed_report(self):
        self.assertEqual(pair(doi_fixture(two_reports=True))['paired_sources'],0)

    def test_incomplete_trace_not_accepted(self):
        trace=doi_fixture();trace['complete']=False
        with self.assertRaisesRegex(ValueError,'Complete'):pair(trace)

    def test_current_arcade_module_has_its_own_identity(self):
        source=(ROOT/'tools/latency/ps4_ketsui_core_probe.h').read_text()
        transformed=header(source,'daioujou_01030.prx')
        self.assertIn('"daioujou_01030.prx"',transformed)
        self.assertNotIn('ketsui_01000.prx',transformed)
        self.assertIn('0x400088',transformed)
        self.assertIn('0x61250',transformed)

    def test_unaudited_module_not_accepted(self):
        with self.assertRaisesRegex(ValueError,'independently audited'):header('','daioujou_99999.prx')

    def response(self):
        tracks={i:dict(x=100+({20:4,21:10,22:14}.get(i,0)),score=.95,distant_score=.3) for i in range(15,23)}
        frames=[dict(present_begin=0,result=0) for _ in range(23)];frames[21]['present_begin']=123
        return tracks,{i:dict(p) for i,p in tracks.items()},frames

    def test_first_ready_is_not_first_submitted_response(self):
        self.assertEqual(response_indices(*self.response()),(20,21))

    def test_response_replacement_must_match_metadata(self):
        a,b,f=self.response();f[20]['present_begin']=122
        with self.assertRaisesRegex(ValueError,'replacement'):response_indices(a,b,f)

    def test_uncertain_body_not_promoted_to_response(self):
        a,b,f=self.response();b[20]['score']=.7
        with self.assertRaisesRegex(ValueError,'Uncertain'):response_indices(a,b,f)

    def test_shad_current_module_and_got_not_ketsui(self):
        result=transform('ketsui_01000.prx +313936 0x5c0a0 0x50088 0x6610 0x29629',0x2006a0)
        self.assertIn('daioujou_01030.prx',result)
        self.assertIn('+2098848',result)
        self.assertIn('0x1b55e',result)
        self.assertNotIn('0x5c0a0',result)

    def shad_response(self):
        tracks={i:dict(x=100+(4 if i==12 else 10 if i>12 else 0),score=.95,distant_score=.3) for i in range(6,17)}
        frames=[dict(present_begin=0,result=0) for _ in range(17)];frames[12]['present_begin']=42
        return tracks,{i:dict(v) for i,v in tracks.items()},frames

    def test_shad_first_response_is_first_submitted(self):
        self.assertEqual(shad_response(*self.shad_response()),12)

    def test_shad_missing_submission_cannot_be_promoted(self):
        a,b,f=self.shad_response();f[12]['present_begin']=0
        with self.assertRaisesRegex(ValueError,'not submitted'):shad_response(a,b,f)


if __name__=='__main__':unittest.main()
