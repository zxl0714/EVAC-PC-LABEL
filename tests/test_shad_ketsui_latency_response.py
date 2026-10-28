"""Keep an inspected shad response distinct from submission or display guesses."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/latency'))
from review_shad_ketsui_pixels import reviewed_response


def fixture():
    rows=[dict(seq=1,qpc=100),dict(seq=2,qpc=110),dict(seq=3,qpc=120),
          dict(seq=111,qpc=90),dict(seq=112,qpc=130),dict(seq=212,qpc=140),dict(seq=312,qpc=150)]
    frames=[dict(prepare=100+i,ready=200+i,present_begin=300+i,result=0) for i in range(16)]
    sample=dict(command=101,source_buttons=0x4008,core_buttons=0x101,
        points=dict(source=1,pad_return=2,delivery_end=3))
    def feature(x):
        return {i:dict(x=x+([4,8,8,8][i-12] if i>=12 else 0),score=.95,distant_score=.4) for i in range(4,16)}
    return dict(records=rows,frequency=1000),dict(frames=frames),sample,feature(205),feature(206)


class ShadResponseTests(unittest.TestCase):
    def test_first_moving_image_is_exact_submitted_image(self):
        result=reviewed_response(*fixture())
        self.assertEqual(result['first_response_snapshot'],12)
        self.assertEqual(result['response_present_seq'],312)
        self.assertEqual(result['core_delivery_to_response_present_ms'],30)
        self.assertIsNone(result['p95_ms'])
        self.assertIn('NOT a GPU completion',result['readiness_definition'])

    def test_animation_uncertainty_or_prior_motion_rejected(self):
        args=fixture();args[3][11]['x']+=3
        with self.assertRaisesRegex(ValueError,'already moving'): reviewed_response(*args)
        args=fixture();args[4][12]['score']=.6
        with self.assertRaisesRegex(ValueError,'Uncertain'): reviewed_response(*args)

    def test_single_frame_step_not_sustained_rejected(self):
        args=fixture();args[3][13]['x']=205
        with self.assertRaisesRegex(ValueError,'displacement'): reviewed_response(*args)

    def test_response_not_submitted_or_failed_rejected(self):
        for key in ('present_begin','result'):
            args=fixture();args[1]['frames'][12][key]=0 if key=='present_begin' else 1
            with self.assertRaisesRegex(ValueError,'successfully submitted'): reviewed_response(*args)

    def test_input_after_first_response_rejected(self):
        args=fixture();args[0]['records'][2]['qpc']=131
        with self.assertRaisesRegex(ValueError,'clock order'): reviewed_response(*args)


if __name__=='__main__': unittest.main()
