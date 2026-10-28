import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from vulkan_present_feedback import analyze


def fixture():
    return [dict(kind='header',schema=1,pid=42,frequency=10_000_000),
            dict(kind='chain',generation=1,domain_id=7,stages=5),
            dict(kind='calibration',generation=1,domain=1000208000,domain_id=7,stage=4,
                 host_qpc=100_000,local_ns=1_000_000_000_000_000_000,max_deviation_ns=50),
            dict(kind='present',generation=1,id=1,source=123,begin_qpc=110_000,end_qpc=112_000,mode=2,result=0),
            dict(kind='stage',generation=1,id=1,stage=4,time=1_000_000_000_003_000_000,
                 domain=1000208000,domain_id=7,complete=True),
            dict(kind='end',generation=1,requests=1,completed=1)]


def run(rows):
    return analyze('\n'.join(json.dumps(r) for r in rows),warmup_ms=0)


class VulkanPresentFeedbackTests(unittest.TestCase):
    def test_exact_identity_and_large_epoch_calibration(self):
        result=run(fixture())
        self.assertEqual(result['metrics']['present_to_first_pixel_out_ms']['p50_ms'],2)
        self.assertEqual(result['samples'][0]['id'],1)
        self.assertNotIn('present_to_first_pixel_visible_ms',result['metrics'])

    def test_id_reuse_on_another_chain_does_not_cross_match(self):
        rows=fixture(); rows[4]['generation']=2
        with self.assertRaises(ValueError): run(rows)

    def test_missing_or_wrong_clock_is_not_zero(self):
        for field,value in (('domain_id',8),('stage',1),('domain',3)):
            rows=fixture(); rows[4][field]=value
            with self.assertRaises(ValueError):run(rows)

    def test_unobserved_output_is_not_zero(self):
        rows=fixture(); rows[4]['time']=0
        with self.assertRaisesRegex(ValueError,'No observed'):run(rows)

    def test_stale_or_reversed_timestamps_rejected(self):
        for time in (1_000_000_002_003_000_000,999_999_999_999_000_000):
            rows=fixture(); rows[4]['time']=time
            with self.assertRaises(ValueError):run(rows)

    def test_partial_duplicate_failed_and_truncated_captures_rejected(self):
        rows=fixture();rows[4]['complete']=False
        duplicate=fixture();duplicate.insert(5,copy.deepcopy(duplicate[4]))
        failed=fixture();failed[3]['result']=-1
        for rows in (rows,duplicate,failed,fixture()[:-1]):
            with self.assertRaises(ValueError):run(rows)

    def test_counted_unmatched_feedback_is_rejected(self):
        rows=fixture();rows[4]['id']=2
        with self.assertRaises(ValueError):run(rows)

    def test_orphan_result_is_rejected_even_with_a_valid_sample(self):
        rows=fixture();extra=copy.deepcopy(rows[4]);extra['id']=2
        rows.insert(5,extra);rows[-1]['completed']=2
        with self.assertRaisesRegex(ValueError,'owner'):run(rows)

    def test_driver_zero_uncertainty_is_not_treated_as_perfect_accuracy(self):
        rows=fixture();extra=copy.deepcopy(rows[2]);extra['max_deviation_ns']=0
        rows.insert(3,extra)
        result=run(rows)
        self.assertEqual(result['counts']['zero_deviation_calibrations_ignored'],1)
        self.assertEqual(result['samples'][0]['first_pixel_out_uncertainty_ms'],.00005)

    def test_bad_optional_queue_clock_does_not_become_zero_or_pollute_output(self):
        rows=fixture();cal=copy.deepcopy(rows[2]);cal['stage']=1
        stamp=copy.deepcopy(rows[4]);stamp.update(stage=1,time=999_999_999_999_000_000)
        rows.insert(3,cal);rows.insert(6,stamp)
        result=run(rows)
        self.assertEqual(result['counts']['queue_end_invalid_clock_samples'],1)
        self.assertNotIn('present_to_queue_end_ms',result['metrics'])
        self.assertEqual(result['metrics']['present_to_first_pixel_out_ms']['p50_ms'],2)


if __name__=='__main__':unittest.main()
