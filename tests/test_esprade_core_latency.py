"""Reject misleading input/core pairings in the private original-code audit."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from esprade_core_latency import analyze


def fixture():
    rows = []

    def add(kind, **values):
        row = dict.fromkeys(('a', 'b', 'c', 'd', 'e', 'f'), 0)
        row.update(seq=len(rows)+1, qpc=len(rows)+1, kind=kind, thread=1)
        row.update(values)
        rows.append(row)
        return row['seq']

    source = add(14, a=301, b=8, c=1, thread=99)
    pad = add(18)
    add(20, a=pad, b=0)  # Older neutral report in the same PadRead batch.
    add(20, a=pad, b=1, c=0x20, f=source)
    add(19, a=pad, b=2, c=2)
    frame = add(10, a=100, b=0xe80, f=0x1001c60b)
    add(10, a=116, b=0x3403f60, c=frame, d=1)
    apply = add(10, a=102, b=0x5cc80, c=frame, e=(3 << 32) | 0x100)
    add(10, a=103, b=0x5cc80, c=apply, d=frame)
    add(10, a=110, b=0x5ca70, c=frame)
    for _ in range(2):
        step = add(10, a=112, b=0x66f60, c=frame, d=0x1234)
        add(10, a=114, b=0x66f60, c=step)
        add(10, a=115, b=0x66f60, c=step)
        add(10, a=113, b=0x66f60, c=step, d=frame)
    add(10, a=101, b=0xe80, c=frame)
    return dict(complete=True, frequency=1000, pid=7, committed=len(rows), overflow=0, records=rows)


class OriginalCorePairingTests(unittest.TestCase):
    def test_accepts_last_report_and_normal_plus_prediction(self):
        result = analyze(fixture(), 301, 301)
        self.assertEqual(result['samples'][0]['points']['last_report'], 4)
        self.assertEqual(len(result['samples'][0]['steps']), 2)

    def test_cannot_choose_an_older_report_with_the_desired_button(self):
        trace = fixture()
        trace['records'][2].update(c=0x20, f=1)
        trace['records'][3].update(c=0, f=0)
        with self.assertRaisesRegex(ValueError, 'Missing first original input callback'):
            analyze(trace, 301, 301)

    def test_rejects_delivery_on_another_thread(self):
        trace = fixture()
        trace['records'][8]['thread'] = 2
        with self.assertRaisesRegex(ValueError, 'thread differs'):
            analyze(trace, 301, 301)

    def test_rejects_incomplete_speculative_execution(self):
        trace = fixture()
        trace['records'] = [r for r in trace['records'] if r['seq'] != 17]
        with self.assertRaisesRegex(ValueError, 'Core step ownership'):
            analyze(trace, 301, 301)

    def test_rejects_foreign_original_dispatch(self):
        trace = deepcopy(fixture())
        trace['records'][5]['f'] += 1
        with self.assertRaisesRegex(ValueError, 'original tail-parent dispatch'):
            analyze(trace, 301, 301)


if __name__ == '__main__':
    unittest.main()
