import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from callback_inventory import forwarding_size, pointer_references


def code(*words):
    return struct.pack('>' + 'I' * len(words), *words)


class CallbackInventoryTests(unittest.TestCase):
    def test_adjustor_and_virtual_forwarding(self):
        self.assertEqual(forwarding_size(code(0x3863fffc, 0x48000020), 0), 8)
        self.assertEqual(forwarding_size(code(0x81630000, 0x816b0020, 0x7d6903a6, 0x4e800420), 0), 16)

    def test_rejects_calls_and_conditional_control_flow(self):
        for word in (0x48000021, 0x48000022, 0x41820020):
            self.assertIsNone(forwarding_size(code(word), 0))

    def test_rejects_self_nops_and_stack_changes(self):
        self.assertIsNone(forwarding_size(code(0x7d084378, 0x48000020), 0))
        self.assertIsNone(forwarding_size(code(0x38210080, 0x48000020), 0))

    def test_requires_explicit_ctr_and_bounded_body(self):
        self.assertIsNone(forwarding_size(code(0x4e800420), 0))
        self.assertIsNone(forwarding_size(code(*([0x38630004] * 8), 0x48000020), 0))
        self.assertIsNone(forwarding_size(b'\x80', 0))

    def test_aligned_unregistered_executable_pointers_only(self):
        data = code(0x1000, 0x1004, 0x1005, 0x2000, 0x1008)
        self.assertEqual(pointer_references(data, 0x8000, [(0x1000, 0x1010)], {0x1004}),
                         {0x1000: [0x8000], 0x1008: [0x8010]})

    def test_offsets_cannot_wrap_or_read_unaligned_instructions(self):
        data = code(0x3863fffc, 0x48000020)
        for offset in (-4, -1, 1, len(data), len(data) + 4):
            with self.subTest(offset=offset):
                self.assertIsNone(forwarding_size(data, offset))


if __name__ == '__main__':
    unittest.main()
