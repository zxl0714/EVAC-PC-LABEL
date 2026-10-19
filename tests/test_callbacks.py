import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from callback_inventory import forwarding_size, pointer_references, readonly_leaf_size


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

    def test_exception_table_tails_are_not_callable_forwarders(self):
        for instructions in ((0x3c608007, 0x6063000e, 0x83df0084, 0x48000010),
                             (0x83df0054, 0x48000020),
                             (0x38600000, 0x83bf0050, 0x48000008)):
            with self.subTest(instructions=instructions):
                self.assertIsNone(forwarding_size(code(*instructions), 0))

    def test_aligned_unregistered_executable_pointers_only(self):
        data = code(0x1000, 0x1004, 0x1005, 0x2000, 0x1008)
        self.assertEqual(pointer_references(data, 0x8000, [(0x1000, 0x1010)], {0x1004}),
                         {0x1000: [0x8000], 0x1008: [0x8010]})

    def test_offsets_cannot_wrap_or_read_unaligned_instructions(self):
        data = code(0x3863fffc, 0x48000020)
        for offset in (-4, -1, 1, len(data), len(data) + 4):
            with self.subTest(offset=offset):
                self.assertIsNone(forwarding_size(data, offset))

    def test_authored_leaf_cfg_includes_both_conditional_returns(self):
        # li r3,0; cmpwi r3,0; beq +12; li r3,1; blr; li r3,2; blr
        fixture = code(0x38600000, 0x2c030000, 0x4182000c, 0x38600001,
                       0x4e800020, 0x38600002, 0x4e800020)
        self.assertEqual(readonly_leaf_size(fixture, 0), 28)
        self.assertIsNone(readonly_leaf_size(fixture, 0, max_bytes=24))

    def test_leaf_cfg_allows_a_bounded_loop_and_rejects_missing_return(self):
        # li r3,0; addi r3,r3,1; cmpwi r3,3; bne -8; blr
        fixture = code(0x38600000, 0x38630001, 0x2c030003, 0x4082fff8, 0x4e800020)
        self.assertEqual(readonly_leaf_size(fixture, 0), 20)
        self.assertIsNone(readonly_leaf_size(code(0x48000000), 0))

    def test_leaf_candidates_reject_calls_frames_stores_and_nonvolatile_state(self):
        for word in (0x48000005, 0x90630000, 0x83df0054, 0x3821ff80, 0x3be00001,
                     0x4e800420, 0x2d030000):
            fixture = code(word, 0x4e800020)
            with self.subTest(word=hex(word)):
                self.assertIsNone(readonly_leaf_size(fixture, 0))
        self.assertEqual(readonly_leaf_size(code(0x60000000, 0x4e800020), 0), 8)

    def test_leaf_candidates_reject_escaping_absolute_unaligned_and_truncated_paths(self):
        for fixture, offset in ((code(0x48000008), 0), (code(0x48000002), 0),
                                (code(0x4bfffffc), 0), (b'abc', 0), (code(0x4e800020), 1)):
            with self.subTest(fixture=fixture, offset=offset):
                self.assertIsNone(readonly_leaf_size(fixture, offset))

    def test_leaf_tail_dispatch_requires_ctr_on_every_incoming_path(self):
        self.assertEqual(readonly_leaf_size(code(0x81630000, 0x7d6903a6, 0x4e800420), 0), 12)
        bypass = code(0x2c030000, 0x41820008, 0x7d6903a6, 0x4e800420)
        self.assertIsNone(readonly_leaf_size(bypass, 0))
        self.assertIsNone(readonly_leaf_size(code(0x7d6903a6, 0x4e800421), 0))


if __name__ == '__main__':
    unittest.main()
