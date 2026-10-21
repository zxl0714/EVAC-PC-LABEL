from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from sdoj_latency import analyze, records
from sdoj_output import associate, output_records
from test_sdoj_latency import chain
from project import ROOT


def sample():
    events = chain() + [dict(seq=7, phase='guest_submit', t_us=700, thread=2,
                            host_iteration=11, render_source_seq=5, packet=0x1000028, valid=True)]
    output = [dict(phase='swap', frame=2, packet=0x1000028, t_us=800),
              dict(phase='ready', frame=2, packet=0x1000028, t_us=900, active=True),
              dict(phase='present', frame=2, packet=0x1000028, begin_us=1000, t_us=1100,
                   hr=0, guest_drawn=True)]
    return events, output


def native_sample():
    events, output = sample()
    events[4]['draw_list'] = 0x41002000
    submit = events.pop()
    draw = dict(seq=7, phase='native_enqueue', t_us=610, thread=2, host_iteration=11,
                render_source_seq=5, queue=0x41003000, slot=0x41004000,
                kind=0, frame_arg=0x41002000, valid=True)
    pop = dict(draw, seq=8, phase='native_dequeue', thread=3, t_us=620)
    end = dict(draw, seq=9, t_us=630, kind=1, frame_arg=0, slot=0x41004008)
    end_pop = dict(end, seq=10, phase='native_dequeue', thread=3, t_us=650)
    submit.update(seq=11, native_dequeue_seq=10, thread=3)
    return events + [draw, pop, end, end_pop, submit], output


class OutputTests(unittest.TestCase):
    def test_first_output_statistics_keep_paused_picture_repeats_separate(self):
        events, output = sample()
        events += [dict(events[4], seq=8, t_us=1200, host_iteration=12),
                   dict(events[5], seq=9, t_us=1300, host_iteration=12),
                   dict(events[6], seq=10, t_us=1400, host_iteration=12, render_source_seq=8, packet=0x2000028)]
        output += [dict(row, frame=3, packet=0x2000028, t_us=row['t_us']+1000,
                        **({'begin_us': 2000} if row['phase']=='present' else {})) for row in output[:]]
        report = associate(events, output, analyze(events))
        self.assertEqual(report['paired_frames'], 2)
        self.assertEqual(report['first_result_frames'], 1)
        self.assertEqual(report['held_result_frames'], 1)
        self.assertEqual(report['first_result_intervals_us']['copy_to_present_return']['max'], 1000)

    def test_exact_packet_and_source_chain(self):
        events, output = sample()
        report = associate(events, output, analyze(events))
        self.assertEqual(report['paired_frames'], 1)
        self.assertEqual(report['intervals_us']['copy_to_present_return']['median'], 1000)
        self.assertEqual(report['intervals_us']['source_to_present_return']['median'], 600)

    def test_identity_and_order_failures_do_not_pair(self):
        for index, change in ((0, dict(packet=1)), (1, dict(packet=1)),
                              (1, dict(active=False)), (1, dict(t_us=1200)),
                              (2, dict(hr=1)), (2, dict(guest_drawn=False)),
                              (2, dict(begin_us=850)), (2, dict(frame=3))):
            events, output = sample()
            output[index].update(change)
            with self.subTest(index=index, change=change):
                self.assertEqual(associate(events, output, analyze(events))['paired_frames'], 0)

    def test_source_thread_iteration_missing_and_ring_reuse(self):
        for changes in (dict(thread=3), dict(host_iteration=12), dict(render_source_seq=4),
                        dict(t_us=400), dict(valid=False)):
            events, output = sample()
            events[-1].update(changes)
            result = associate(events, output, analyze(events))
            self.assertEqual(result['paired_frames'], 0)
        events, output = sample()
        events.append(dict(events[-1], seq=8, t_us=750))
        self.assertEqual(associate(events, output, analyze(events))['paired_frames'], 0)

    def test_other_thread_requires_exact_native_queue_and_draw_list(self):
        events, output = native_sample()
        result = associate(events, output, analyze(events))
        self.assertEqual(result['paired_frames'], 1)
        self.assertEqual(result['intervals_us']['native_queue_wait']['median'], 20)
        self.assertEqual(result['intervals_us']['draw_queue_wait']['median'], 10)
        events[4]['draw_list'] = 0x41002004
        rejected = associate(events, output, analyze(events))
        self.assertEqual(rejected['paired_frames'], 0)
        self.assertEqual(rejected['paired_submit_to_present'], 1)

    def test_frame_end_is_not_a_draw_and_missing_or_wrong_dispatch_rejected(self):
        for index, change in ((6, dict(frame_arg=0x41002004)),
                              (6, dict(kind=1)), (7, dict(frame_arg=0x41002004)),
                              (6, dict(thread=99)), (6, dict(host_iteration=12)),
                              (7, dict(queue=0x41003008)), (7, dict(valid=False)),
                              (8, dict(slot=0x41004000))):
            events, output = native_sample()
            events[index].update(change)
            with self.subTest(index=index, change=change):
                self.assertEqual(associate(events, output, analyze(events))['paired_frames'], 0)

    def test_callback_wait_is_paired_by_native_dequeue_not_time_only(self):
        events, output = native_sample()
        events[-1]['seq'] = 13
        begin = dict(seq=11, phase='native_wait_begin', t_us=660, thread=3,
                     host_iteration=11, native_dequeue_seq=10, wait_flag=0)
        end = dict(begin, seq=12, phase='native_wait_end', t_us=690)
        events[-1:-1] = [begin, end]
        report = associate(events, output, analyze(events))
        self.assertEqual(report['intervals_us']['native_callback_wait']['median'], 30)
        end['native_dequeue_seq'] = 9
        report = associate(events, output, analyze(events))
        self.assertEqual(report['paired_frames'], 1)
        self.assertNotIn('native_callback_wait', report['intervals_us'])

    def test_duplicate_swap_and_repeat_presents(self):
        events, output = sample()
        self.assertEqual(associate(events, output + [output[0]], analyze(events))['paired_frames'], 0)
        report = associate(events, output + [dict(output[-1], begin_us=1200, t_us=1300)], analyze(events))
        self.assertEqual(report['paired_frames'], 1)
        self.assertEqual(report['frames'][0]['present']['t_us'], 1100)

    def test_raw_hex_not_decimal_and_missing_fields(self):
        result = output_records(['OUTPUT_LATENCY phase=present frame=1 packet=01000028 t_us=2 begin_us=1 hr=00000000 guest_drawn=true'])
        self.assertEqual(result[0]['packet'], 0x1000028)
        self.assertEqual(result[0]['hr'], 0)
        submit = records(['SDOJ_LATENCY seq=1 phase=guest_submit t_us=1 thread=2 host_iteration=1 packet=01000028'])[0]
        self.assertEqual(submit['packet'], 0x1000028)
        with self.assertRaises(ValueError):
            output_records(['OUTPUT_LATENCY phase=ready frame=1 t_us=1'])

    def test_default_off_host_metadata_not_guest_packet_writes(self):
        presenter = (ROOT / 'third_party/rexglue/src/ui/presenter.cpp').read_text(encoding='utf-8')
        self.assertIn('REXCVAR_DEFINE_BOOL(output_latency_trace, false,', presenter)
        self.assertIn('diagnostic_packet = diagnostic_packet;', presenter)
        submit = (ROOT / 'games/sdoj/diagnostics/latency_trace.cpp').read_text(encoding='utf-8')
        self.assertIn('physical + 28', submit)
        for forbidden in ('REX_STORE_', 'Sleep(', 'XamInputGetState('):
            self.assertNotIn(forbidden, submit)

    def test_output_identity_is_not_guest_vblank_counter(self):
        worker = (ROOT / 'third_party/rexglue/src/graphics/command_processor.cpp').read_text(encoding='utf-8')
        section = worker[worker.index('const bool trace_output'):worker.index('uint32_t magic =', worker.index('const bool trace_output'))]
        self.assertIn('swap_diagnostic_frame_ < 65536', section)
        self.assertIn('++swap_diagnostic_frame_', section)
        self.assertNotIn('counter_', section)
        backend = (ROOT / 'third_party/rexglue/src/graphics/d3d12/command_processor.cpp').read_text(encoding='utf-8')
        self.assertIn('swap_diagnostic_packet_ ? swap_diagnostic_frame_ : 0', backend)


if __name__ == '__main__':
    unittest.main()
