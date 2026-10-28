import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from futari_latency import analyze, SITE_ADDRESSES
from test_latency_probe import event, trace


def fixture():
    rows = []
    def add(kind, *args, thread=10):
        seq = len(rows) + 1
        rows.append(event(seq, seq * 10, kind, *args, thread=thread))
        return seq
    def site(number, *args, thread=10):
        return add(10, number, SITE_ADDRESSES[number], *args, thread=thread)
    source = add(14, 101, 8, 1, thread=30)
    site(18, 99, 1000)
    site(21, 0, 8888, 0, thread=20)
    begin = add(3, 0, thread=20)
    add(15, source, 0, source, 8, thread=20)
    add(4, begin, 0, 0, source, 8, thread=20)
    site(4, 8, 0x886998f4, 0, thread=20)
    site(5, 0x10010, 0x886998f4, 0, thread=20)
    site(11, 0x886998f4, thread=20)
    site(12, thread=20)
    site(13, thread=20)
    site(16, 20, 4096, 8192, 12288, thread=20)
    site(14, thread=20)
    site(20, 77, 1764, 0, thread=20)
    site(19, 0, 1000)
    site(15)
    site(17, 4096, 4176, 8192, 12288)
    site(1, 1)
    site(6)
    add(13, 128, 256, 1280, 720)
    swap = add(6, 128, 256, 1280, 720, thread=40)
    ready = add(7, swap, 0, 1280, 720, 1, thread=40)
    present = add(8, ready, 500, 0, 1, 1, thread=40)
    add(9, present, ready, 0, thread=40)
    site(18, 99, 1000)
    return rows


class FutariLatencyTests(unittest.TestCase):
    def test_handshake_and_exact_render_list_are_paired(self):
        report = analyze(trace(fixture()))
        self.assertEqual(report['paired_sources'], 1)
        self.assertEqual(report['modes'], {1: 1})
        self.assertEqual(report['samples'][0]['points']['present_begin'], 23)
        self.assertIn('first_changed_pixel', report['unmeasured'])

    def test_wrong_count_pointer_owner_and_lock_are_rejected(self):
        for field in ('c', 'd', 'e', 'f'):
            rows = fixture()
            next(r for r in rows if r['kind'] == 10 and r['a'] == 16)[field] += 1
            self.assertEqual(analyze(trace(rows))['paired_sources'], 0)

    def test_wrong_completion_owner_and_extra_update_rejected(self):
        for site_id, field in ((20, 'd'), (21, 'e'), (19, 'c'), (5, 'd')):
            rows = fixture()
            next(r for r in rows if r['kind'] == 10 and r['a'] == site_id)[field] += 1
            self.assertEqual(analyze(trace(rows))['paired_sources'], 0)

    def test_missing_stage_and_wrong_opcode_site_address_rejected(self):
        for remove in (False, True):
            rows = fixture()
            row = next(r for r in rows if r['kind'] == 10 and r['a'] == 13)
            if remove:
                rows.remove(row)
            else:
                row['b'] += 4
            self.assertEqual(analyze(trace(rows))['paired_sources'], 0)

    def test_failed_present_does_not_become_input_output(self):
        rows = fixture()
        next(r for r in rows if r['kind'] == 9)['c'] = 1
        self.assertEqual(analyze(trace(rows))['paired_sources'], 0)

    def test_earlier_core_sample_is_not_skipped(self):
        rows = fixture()
        # A source already reached the core before the observed cycle. A later
        # complete cycle cannot silently stand in for its first response.
        rows.append(event(26, 15, 15, 1, 0, 1, 8, thread=20))
        self.assertEqual(analyze(trace(rows))['paired_sources'], 0)

    def test_overflow_rejected(self):
        capture = trace(fixture())
        capture['complete'] = False
        with self.assertRaisesRegex(ValueError, 'Complete trace'):
            analyze(capture)


if __name__ == '__main__':
    unittest.main()
