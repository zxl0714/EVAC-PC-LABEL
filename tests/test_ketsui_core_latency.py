"""Use the last original report and first owned callback, not time-near frames."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from ketsui_core_latency import analyze


def fixture(bad_first=False,two_reports=False):
    rows=[]
    def emit(kind,*values,thread=10):
        seq=len(rows)+1
        rows.append(dict(seq=seq,qpc=seq*10,thread=thread,kind=kind,
            **dict(zip(('a','b','c','d','e','f'),(*values,0,0,0,0,0,0)))))
        return seq
    source=emit(14,101,0x4008,1)
    release=emit(14,101,0,0) if two_reports else None
    def cycle(buttons):
        began=emit(18,1,63,0x7000,0x101ea6d6)
        emit(20,began,0,0x8020,1,1,source)
        if two_reports:
            emit(20,began,1,0,2,1,release)
        count=2 if two_reports else 1
        emit(19,began,count,count)
        frame=emit(10,100,0x6610,0x1000,0x2000,0x3000,0x10029629)
        delivery=emit(10,102,0x5c0a0,frame,0,(3<<32)|buttons,0x4000)
        emit(10,103,0x5c0a0,delivery,frame,0,0)
        emit(10,101,0x6610,frame,0,0x5000,0)
    cycle(0 if bad_first else 0x101)
    if bad_first:
        cycle(0x101)
    return dict(pid=1,frequency=1000000,capacity=1000,committed=len(rows),reserved=len(rows),
        complete=True,incomplete=0,overflow=0,records=rows)


class KetsuiCoreLatencyTests(unittest.TestCase):
    def test_exact_last_report_and_owned_callback(self):
        result=analyze(fixture())
        self.assertEqual(result['paired_sources'],1)
        self.assertEqual(result['samples'][0]['core_buttons'],0x101)
        self.assertIn('first_response_pixels',result['unmeasured'])
        self.assertNotIn('source_to_present_us',result['statistics'])

    def test_first_failed_callback_cannot_be_replaced(self):
        result=analyze(fixture(bad_first=True))
        self.assertEqual(result['paired_sources'],0)
        self.assertEqual(result['unpaired_commands'],[101])

    def test_earlier_pressed_report_not_last_is_not_consumption(self):
        self.assertEqual(analyze(fixture(two_reports=True))['paired_sources'],0)

    def test_actual_core_button_mask_required(self):
        trace=fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==102)['e']=(3<<32)|0x81
        self.assertEqual(analyze(trace)['paired_sources'],0)

    def test_wrong_original_caller_rejected(self):
        trace=fixture()
        next(r for r in trace['records'] if r['kind']==10 and r['a']==100)['f']+=4
        with self.assertRaisesRegex(ValueError,'original callback caller'):
            analyze(trace)

    def test_different_poll_thread_is_not_reassociated_globally(self):
        trace=fixture()
        for r in trace['records']:
            if r['kind'] in (18,19,20):
                r['thread']=20
        self.assertEqual(analyze(trace)['paired_sources'],0)


if __name__=='__main__':
    unittest.main()
