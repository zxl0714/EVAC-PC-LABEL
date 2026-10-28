import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from vulkan_wsi_crosscheck import crosscheck


def benchmark():
    rows=[dict(kind='header',schema=1,pid=42,frequency=10_000_000),dict(kind='chain',generation=1)]
    for i,qpc in ((1,100_000),(2,300_000),(3,500_000)):
        rows += [dict(kind='present',generation=1,id=i,source=i-1,begin_qpc=qpc,end_qpc=qpc+1000,result=0),
                 dict(kind='work',generation=1,id=i,frame=i-1,begin_qpc=qpc-3000,acquired_qpc=qpc-2000,
                      submitted_qpc=qpc-1000,retired_qpc=qpc+2000)]
    rows.append(dict(kind='end',requests=3))
    return rows


def event(qpc=100_000,**updates):
    return dict(ProcessID='42',SwapChainAddress='0x100',PresentRuntime='DXGI',
                CPUStartQPC=str(qpc),CPUBusy='.2',CPUWait='.1',FrameTime='.3',
                DisplayLatency='1',DisplayedTime='16.67',PresentMode='Hardware: Independent Flip') | updates


def run(capture=None,rows=None):
    return crosscheck('\n'.join(json.dumps(r) for r in (capture or benchmark())),rows or [event()],warmup_ms=0)


class VulkanWsiCrosscheckTests(unittest.TestCase):
    def test_async_call_after_return_has_a_bounded_work_owner(self):
        sample=run()['samples'][0]
        self.assertEqual(sample['id'],1)
        self.assertEqual(sample['request_to_display_ms'],1)
        self.assertEqual(sample['request_to_driver_entry_ms'],.2)

    def test_duplicate_and_cross_request_calls_rejected(self):
        for rows in ([event(),event(101_000)], [event(CPUWait='20',FrameTime='20.2')]):
            with self.assertRaises(ValueError):run(rows=rows)

    def test_multiple_swapchains_and_unknown_runtime_rejected(self):
        for rows in ([event(),event(300_000,SwapChainAddress='0x200')], [event(PresentRuntime='Other')]):
            with self.assertRaises(ValueError):run(rows=rows)

    def test_other_pid_does_not_gain_an_owner(self):
        with self.assertRaisesRegex(ValueError,'No bounded'):run(rows=[event(ProcessID='43')])

    def test_generated_frames_or_unobserved_output_not_zero(self):
        for update in (dict(FrameType='Interpolated'),dict(DisplayLatency='NA',DisplayedTime='NA')):
            with self.assertRaises(ValueError):run(rows=[event(**update)])

    def test_game_trace_without_single_producer_work_cannot_use_this_matcher(self):
        rows=benchmark();rows=[r for r in rows if r['kind']!='work']
        with self.assertRaises(ValueError):run(capture=rows)

    def test_overlapping_or_missing_requests_rejected(self):
        for field,value in (('begin_qpc',100_100),('source',99)):
            rows=copy.deepcopy(benchmark());rows[4][field]=value
            with self.assertRaises(ValueError):run(capture=rows)


if __name__=='__main__':unittest.main()
