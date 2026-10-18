from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from espgaluda2.analyze_community import in_analysis, view_table


def row(view=1, combined=0, character=-1, mode=21, flag=0, category=0):
    return struct.pack('>IIiIII', view, combined, character, mode, flag, category)


END = struct.pack('>I', 0xffffffff)


class CommunityAnalysisTests(unittest.TestCase):
    def test_big_endian_metadata_and_unknown_selector_labels(self):
        result = view_table(row() + row(9, character=2, mode=26, category=2) + END, 0)
        self.assertEqual(result[0], dict(view=1, combinedPlayers=0, character=-1,
                                        mode=21, flag=0, category=0))
        self.assertEqual(result[1]['view'], 9)
        self.assertEqual(result[1]['category'], 2)

    def test_aligned_offset_and_no_bytes_past_terminator_needed(self):
        self.assertEqual(len(view_table(b'HEAD' + row() + END, 4)), 1)
        for offset in (-4, 1, 800):
            with self.assertRaises(ValueError):
                view_table(row() + END, offset)

    def test_empty_truncated_and_unterminated_tables(self):
        for data in (b'', END, row()[:-1], row(), row() + END[:3]):
            with self.assertRaises(ValueError):
                view_table(data, 0)

    def test_duplicate_and_unsupported_metadata_rejected(self):
        for data in (row() + row(), row(0), row(combined=2), row(character=3),
                     row(mode=19), row(flag=4), row(category=3)):
            with self.assertRaises(ValueError):
                view_table(data + END, 0)

    def test_hard_scan_bound(self):
        for limit in (0, 257, 1):
            with self.assertRaises(ValueError):
                view_table(row() + END, 0, limit)

    def test_census_path_cannot_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            analysis = Path(directory).resolve()
            self.assertEqual(in_analysis(analysis, 'default/data'), analysis / 'default/data')
            with self.assertRaises(ValueError):
                in_analysis(analysis, '../escape')


if __name__ == '__main__':
    unittest.main()
