"""Private image lineage must be checked independently of character response."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
sys.path.insert(0,str(ROOT/'tools/latency'))
from vulkan_pixel_probe import read_capture
from prepare_ps4_vulkan_pixels import instrument_presenter


class VulkanPixelProbeTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.prefix=Path(self.folder.name)/'pixels'
        self.text=('# evac_vk_pixels_v1 frequency=1000000 pid=2 width=1280 height=720 x=320 y=360 '
            'roi_width=2 roi_height=1 format=50 pitch=8 stride=256 capacity=3\n'
            'index,prepare,copy_qpc,ready,present_begin,present_end,result\n'
            '0,1,20,2,3,4,0\n# end captured=1 valid=1 overflow=0 failed=0 bytes_ok=1\n')
        Path(str(self.prefix)+'.bin').write_bytes(b'\0'*256)
        self.trace=dict(complete=True,pid=2,frequency=1000000,records=[
            dict(seq=1,kind=26,qpc=10,thread=5,a=4096,b=1280,c=720),
            dict(seq=2,kind=21,qpc=30,thread=5,a=1,c=4096),
            dict(seq=3,kind=22,qpc=40,thread=6,a=2),
            dict(seq=4,kind=23,qpc=50,thread=6,a=3,b=2,c=0)])

    def read(self,text=None,trace=None):
        Path(str(self.prefix)+'.csv').write_text(text or self.text)
        return read_capture(self.prefix,trace or self.trace)

    def test_valid_owned_image_is_not_automatically_a_response(self):
        result=self.read()
        self.assertEqual(result['frames'][0]['prepare'],1)
        self.assertNotIn('latency_ms',result)

    def test_wrong_ready_prepare_or_address_rejected(self):
        for field in ('a','c'):
            trace=copy.deepcopy(self.trace);trace['records'][1][field]+=1
            with self.assertRaisesRegex(ValueError,'ownership|omitted/changed'):
                self.read(trace=trace)

    def test_wrong_present_ready_or_thread_rejected(self):
        for index,field in ((2,'a'),(3,'b'),(3,'thread')):
            trace=copy.deepcopy(self.trace);trace['records'][index][field]+=1
            with self.assertRaisesRegex(ValueError,'ownership|omitted/changed'):
                self.read(trace=trace)

    def test_overflow_or_incomplete_rejected(self):
        for old,new in (('overflow=0','overflow=1'),('failed=0','failed=1'),('valid=1','valid=0')):
            with self.assertRaises(ValueError): self.read(self.text.replace(old,new))

    def test_size_clock_and_copy_order_rejected(self):
        for old,new in (('stride=256','stride=512'),('frequency=1000000','frequency=1'),
                        ('0,1,20,2','0,1,5,2')):
            with self.assertRaises(ValueError): self.read(self.text.replace(old,new))

    def test_replaced_ready_image_can_remain_unpresented(self):
        trace=copy.deepcopy(self.trace);trace['records']=trace['records'][:2]
        result=self.read(self.text.replace('0,1,20,2,3,4,0','0,1,20,2,0,0,0'),trace)
        self.assertEqual(result['frames'][0]['present_begin'],0)

    def test_canceled_copy_can_remain_without_ready(self):
        trace=copy.deepcopy(self.trace);trace['records']=trace['records'][:1]
        result=self.read(self.text.replace('0,1,20,2,3,4,0','0,1,20,0,0,0,0'),trace)
        self.assertEqual(result['frames'][0]['ready'],0)

    def test_metadata_cannot_claim_a_presented_image_was_replaced(self):
        with self.assertRaisesRegex(ValueError,'omitted/changed first actual'):
            self.read(self.text.replace('0,1,20,2,3,4,0','0,1,20,2,0,0,0'))
        with self.assertRaisesRegex(ValueError,'omitted/changed actual image readiness'):
            self.read(self.text.replace('0,1,20,2,3,4,0','0,1,20,0,0,0,0'))

    def test_next_image_may_be_ready_before_previous_present(self):
        text=self.text.replace('capacity=3','capacity=3').replace(
            '0,1,20,2,3,4,0\n# end captured=1 valid=1',
            '0,1,20,2,3,4,0\n1,5,32,6,7,8,0\n# end captured=2 valid=2')
        trace=copy.deepcopy(self.trace)
        trace['records'] += [dict(seq=5,kind=26,qpc=31,thread=5,a=8192,b=1280,c=720),
            dict(seq=6,kind=21,qpc=35,thread=5,a=5,c=8192),
            dict(seq=7,kind=22,qpc=55,thread=6,a=6),
            dict(seq=8,kind=23,qpc=60,thread=6,a=7,b=6,c=0)]
        Path(str(self.prefix)+'.bin').write_bytes(b'\0'*512)
        self.assertEqual(len(self.read(text,trace)['frames']),2)

    def test_private_generator_fails_on_changed_anchor(self):
        with self.assertRaises(ValueError): instrument_presenter('wrong source')

    def test_readback_contains_no_submit_wait_or_gameplay_file_write(self):
        text=(ROOT/'tools/latency/vulkan_pixel_probe.h').read_text()
        prepare=text[text.index('  unsigned Prepare('):text.index('  void Completed(')]
        self.assertIn('copyImageToBuffer',prepare)
        for forbidden in ('waitForFences','waitIdle','WriteFile','Flush(','scheduler.Finish','submit2'):
            self.assertNotIn(forbidden,prepare)
        generator=(ROOT/'tools/latency/prepare_ps4_vulkan_pixels.py').read_text()
        self.assertIn('latency_pixels.Flush();',generator)
        self.assertIn('frame.copy_pending = false;',generator)


if __name__=='__main__': unittest.main()
