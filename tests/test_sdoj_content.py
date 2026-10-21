import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
from sdoj_content import movement, snapshots


def line(payload, **changes):
    fields = dict(module=2, generation=1, copy=9, batch=9, t_us=100,
                  host_iteration=8, buffer='41000000', cursor=f'{0x41000000 + len(payload) * 4:08X}',
                  mask='00000008', baseline='false', valid='true', total=len(payload), offset=0,
                  words=''.join(f'{word:08X}' for word in payload))
    fields.update(changes)
    return 'SDOJ_CONTENT ' + ' '.join(f'{key}={value}' for key, value in fields.items())


class ContentTests(unittest.TestCase):
    def test_complete_exact_hex_and_packet(self):
        capture = snapshots([line([0x11000000, 4, (100 << 16) | 200, 1, 2, 3, 4, 5, 6, 0xffffffff])])[0]
        self.assertTrue(capture['complete'])
        self.assertEqual(capture['buffer'], 0x41000000)
        self.assertEqual((capture['packets'][0]['x'], capture['packets'][0]['y']), (100, 200))

    def test_missing_duplicate_short_and_bad_sentinel_rejected(self):
        valid = [0x11000000, 0, 0, 1, 2, 3, 4, 5, 6, 0xffffffff]
        for rows in ([line(valid, offset=1)], [line(valid), line(valid)],
                     [line(valid, total=11)], [line(valid[:-1] + [0])],
                     [line([1] + valid[1:])], [line(valid[:-2] + [0xffffffff])]):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                snapshots(rows)

    def test_runtime_rejection_not_success(self):
        result = snapshots([line([], valid='false', words='-')])[0]
        self.assertFalse(result['complete'])
        self.assertEqual(result['issue'], 'runtime_rejected')

    def test_numeric_only_chunks_preserve_leading_zeros(self):
        words = [0x11000000, 4, 5, 1, 2, 3, 4, 5, 6] * 2 + [0xffffffff]
        rows = [line(words[left:right], total=len(words), offset=left,
                     cursor=f'{0x41000000 + len(words) * 4:08X}')
                for left, right in ((0, 8), (8, 17), (17, 19))]
        capture = snapshots(rows)[0]
        self.assertTrue(capture['complete'])
        self.assertEqual(len(capture['packets']), 2)

    def test_atlas_offset_is_not_player_motion(self):
        def packet(index, y=0, atlas=0, uv=1):
            return dict(index=index, header=0x11000000, uv=uv,
                        x=100 + index + atlas, y=200 + y, attributes=[index, 2, 3, 4, 5, 6])
        before = dict(complete=True, module=2, generation=1, copy=9, mask=0,
                      packets=[packet(index) for index in range(4)])
        after = dict(before, copy=10, mask=8,
                     packets=[packet(index, 3 if index == 3 else 0, 384,
                                     2 if index == 3 else 1) for index in range(4)])
        result = movement(before, after)
        self.assertEqual(result['common_translation'], [384, 0])
        self.assertEqual(result['translation_votes'], 3)
        self.assertEqual(len(result['candidates']), 1)
        self.assertEqual((result['candidates'][0]['relative_dx'], result['candidates'][0]['relative_dy']), (0, 3))
        self.assertIsNone(movement(before, dict(after, generation=2)))
        self.assertIsNone(movement(before, dict(after, copy=11)))


if __name__ == '__main__':
    unittest.main()
