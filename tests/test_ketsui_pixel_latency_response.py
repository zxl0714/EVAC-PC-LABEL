"""A first responsive snapshot is not necessarily the first submitted response."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/latency'))
from review_ketsui_pixels import reviewed_response


def fixture():
    rows=[dict(seq=1,kind=14,a=101,c=1,qpc=1000),dict(seq=2,qpc=1010),dict(seq=3,qpc=1020),
          dict(seq=4,kind=14,a=101,c=0,qpc=1800)]
    frames=[dict(index=i,prepare=100+i,ready=200+i,present_begin=0,present_end=0,result=0) for i in range(22)]
    frames[19]['present_begin']=319;frames[20]['present_begin']=320
    rows += [dict(seq=117,qpc=990),dict(seq=118,qpc=1100),dict(seq=218,qpc=1200),
             dict(seq=219,qpc=1300),dict(seq=319,qpc=1600)]
    for row in rows:
        row.setdefault('kind',0)
    trace=dict(records=rows,frequency=1000)
    sample=dict(command=101,source_buttons=0x4008,core_buttons=0x101,
        points=dict(source=1,pad_return=2,delivery_end=3))
    primary={i:dict(x=205+(i-17)*4 if i>=18 else 205,score=.95,distant_score=.4) for i in range(12,22)}
    secondary={i:dict(primary[i],x=primary[i]['x']+8) for i in (12,14,16,18,20)}
    return trace,dict(frames=frames),sample,primary,secondary


class KetsuiPixelResponseTests(unittest.TestCase):
    def test_reports_actual_submitted_response_not_replaced_first_image(self):
        result=reviewed_response(*fixture())
        self.assertEqual(result['first_response_snapshot'],18)
        self.assertFalse(result['first_response_snapshot_presented'])
        self.assertEqual(result['first_submitted_response_snapshot'],19)
        self.assertEqual(result['response_present_seq'],319)
        self.assertEqual(result['core_delivery_to_response_present_ms'],580)
        self.assertIsNone(result['p95_ms'])
        self.assertIn('panel_light',result['unmeasured'])

    def test_bad_feature_or_moving_baseline_rejected(self):
        args=fixture();args[3][17]['x']+=3
        with self.assertRaisesRegex(ValueError,'already moving'): reviewed_response(*args)
        args=fixture();args[3][18]['score']=.6
        with self.assertRaisesRegex(ValueError,'Uncertain'): reviewed_response(*args)

    def test_no_sustained_movement_or_secondary_disagreement_rejected(self):
        args=fixture();args[3][19]['x']=args[3][18]['x']
        with self.assertRaisesRegex(ValueError,'continuation'): reviewed_response(*args)
        args=fixture();args[4][18]['x']+=2
        with self.assertRaisesRegex(ValueError,'disagree'): reviewed_response(*args)

    def test_wrong_command_or_direction_rejected(self):
        for field,value in (('command',102),('source_buttons',0x4004),('core_buttons',0x81)):
            args=fixture();args[2][field]=value
            with self.assertRaisesRegex(ValueError,'first right'): reviewed_response(*args)

    def test_changed_first_response_submission_cannot_silently_move_endpoint(self):
        args=fixture();args[1]['frames'][19]['present_begin']=0
        with self.assertRaisesRegex(ValueError,'submission changed'): reviewed_response(*args)
        args=fixture();args[1]['frames'][18]['present_begin']=318
        with self.assertRaisesRegex(ValueError,'submission changed'): reviewed_response(*args)

    def test_response_preceding_input_rejected(self):
        args=fixture();args[0]['records'][5]['qpc']=1000
        with self.assertRaisesRegex(ValueError,'brackets'): reviewed_response(*args)


if __name__=='__main__': unittest.main()
