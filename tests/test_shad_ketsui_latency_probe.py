"""Keep shad queue source identity and bounded private build mechanics explicit."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
sys.path.insert(0,str(ROOT/'tools/latency'))
from shad_ketsui_core_latency import attach_sources
from prepare_shad_ketsui_probe import core_header,pixel_header
from rebuild_shad_probe import shorten_link


def fixture():
    rows=[dict(seq=1,kind=14,qpc=10,a=101,b=8,c=1),
          dict(seq=2,kind=27,qpc=20,a=100,b=1,c=1000,d=0x20),
          dict(seq=3,kind=18,qpc=30,f=100),
          dict(seq=4,kind=20,qpc=40,a=3,c=0x20,d=1000,f=0)]
    return dict(records=rows)


class ShadKetsuiProbeTests(unittest.TestCase):
    def test_exact_controller_timestamp_and_buttons_resolve_source(self):
        trace=fixture();result=attach_sources(trace)
        self.assertEqual(result['records'][3]['f'],1)
        self.assertEqual(trace['records'][3]['f'],0)

    def test_other_controller_timestamp_or_buttons_not_guessed(self):
        for field in ('c','d'):
            trace=fixture();trace['records'][3][field]+=1
            self.assertEqual(attach_sources(trace)['records'][3]['f'],0)
        trace=fixture();trace['records'][2]['f']+=1
        self.assertEqual(attach_sources(trace)['records'][3]['f'],0)

    def test_wrong_queued_bits_rejected(self):
        trace=fixture();trace['records'][1]['d']+=1
        with self.assertRaises(ValueError): attach_sources(trace)

    def test_queue_timestamp_after_return_rejected(self):
        trace=fixture();trace['records'][1]['qpc']=50
        with self.assertRaisesRegex(ValueError,'follows'): attach_sources(trace)

    def test_ambiguous_same_controller_timestamp_rejected(self):
        trace=fixture();trace['records'] += [dict(seq=5,kind=14,qpc=11,a=102,b=8,c=1),
            dict(seq=6,kind=27,qpc=21,a=100,b=5,c=1000,d=0x20)]
        with self.assertRaisesRegex(ValueError,'Ambiguous'): attach_sources(trace)

    def test_core_adapter_preserves_original_callback_and_runtime_base(self):
        header=core_header((ROOT/'tools/latency/ps4_ketsui_core_probe.h').read_text(),313936)
        self.assertIn('original_frame(context, inputs, output)',header)
        self.assertIn('original_apply(context,player,report,cache)',header)
        self.assertIn('module->GetBaseAddress()+0x50088',header)
        self.assertIn('provider->GetBaseAddress()+0x5c0a0',header)
        self.assertNotIn('Runtime&',header)

    def test_pixel_wait_is_confined_to_teardown(self):
        header=pixel_header((ROOT/'tools/latency/vulkan_pixel_probe.h').read_text())
        before_flush=header[:header.index('  void Flush()')]
        self.assertNotIn('waitSemaphores',before_flush)
        self.assertIn('frames[i].completed = frames[i].ready != 0',header)
        self.assertIn('std::at_quick_exit',header)

    def test_link_response_is_not_expanded_by_cmake_wrapper(self):
        with tempfile.TemporaryDirectory() as folder:
            step=shorten_link(dict(command=['cmake','-E','vs_link_exe','--','lld-link.exe','a'*40000]),Path(folder))
            self.assertEqual(step['command'][0],'lld-link.exe')
            self.assertLess(sum(map(len,step['command'])),1000)
            text=Path(step['response_file']['path']).read_text()
            self.assertIn('/MANIFEST:EMBED,ID=1',text)
            self.assertIn('a'*40000,text)


if __name__=='__main__': unittest.main()
