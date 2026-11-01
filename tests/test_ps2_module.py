from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from ps2_module import analysis_elf, inspect, module_roots
from ps2_disc import elf_info


def fixture():
    data = bytearray(64+32+16)
    struct.pack_into('<4s7I', data, 0, b'MWo3', 16, 0x730800, 32, 16, 128, 0, 0)
    data[32:43] = b'fixture.bin'
    data[64:] = bytes(range(48))
    return data


class Ps2ModuleTests(unittest.TestCase):
    def test_exact_payload_and_separate_text_data_bss(self):
        data = fixture()
        module = inspect(data)
        self.assertEqual((module.module_id, module.name, module.bss_size), (16, 'fixture.bin', 128))
        elf = analysis_elf(data, 0x730840)
        segment = elf_info(elf)['segments'][0]
        self.assertEqual(elf[segment['offset']:segment['offset']+segment['fileSize']], data)
        self.assertEqual(segment['memorySize'], len(data)+128)
        table = struct.unpack_from('<I', elf, 32)[0]
        text = struct.unpack_from('<10I', elf, table+80)
        initialized = struct.unpack_from('<10I', elf, table+120)
        bss = struct.unpack_from('<10I', elf, table+160)
        self.assertEqual((text[2], text[3], text[5]), (7, 0x730840, 32))
        self.assertEqual((initialized[2], initialized[5], bss[1], bss[5]), (3, 16, 8, 128))

    def test_header_sizes_and_memory_bounds(self):
        for offset, value in ((0, 0), (8, 0x1fffff0), (12, 0), (16, 17), (20, 0xffffffff)):
            data = fixture()
            struct.pack_into('<I', data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                inspect(data)
        for data in (fixture()[:60], fixture()[:-1], fixture()+b'\0'):
            with self.assertRaises(ValueError):
                inspect(data)

    def test_data_cannot_be_an_entry_or_filename(self):
        for entry in (0x730800, 0x730841, 0x730860, 0x100000):
            with self.subTest(entry=entry), self.assertRaises(ValueError):
                analysis_elf(fixture(), entry)
        data = fixture()
        data[32] = ord('/')
        with self.assertRaises(ValueError):
            inspect(data)

    def test_verified_roots_are_function_symbols_without_changing_payload(self):
        data = fixture()
        elf = analysis_elf(data, 0x730840, [0x730850, 0x730840])
        table = struct.unpack_from('<I', elf, 32)[0]
        symbols = struct.unpack_from('<10I', elf, table+7*40)
        self.assertEqual((symbols[1], symbols[5], symbols[6], symbols[9]), (2, 48, 6, 16))
        entries = [struct.unpack_from('<IIIBBH', elf, symbols[4]+16*i) for i in (1,2)]
        self.assertEqual([(entry[1],entry[3],entry[5]) for entry in entries],
                         [(0x730840,0x12,2), (0x730850,0x12,2)])
        self.assertIn(b'root_ov16_00730840\0',elf)
        other=fixture();struct.pack_into('<I',other,4,17)
        self.assertIn(b'root_ov17_00730840\0',analysis_elf(other,0x730840))
        self.assertNotIn(b'root_ov16_00730840\0',analysis_elf(other,0x730840))
        with self.assertRaises(ValueError):
            analysis_elf(data, 0x730840, [0x730860])

    def test_sector_padding_does_not_move_bss_or_become_executable(self):
        original=fixture()
        padded=original+bytes(2048-len(original))
        self.assertEqual(inspect(padded).text_size,32)
        elf=analysis_elf(padded,0x730840)
        segment=elf_info(elf)['segments'][0]
        self.assertEqual((segment['fileSize'],segment['memorySize']),(len(original),len(original)+128))
        self.assertEqual(elf[0x1000:0x1800],padded)
        padded[-1]=1
        with self.assertRaises(ValueError):
            inspect(padded)

    def test_original_direct_calls_seed_only_their_bank(self):
        boot_data=fixture()
        struct.pack_into('<I',boot_data,8,0x100000)
        struct.pack_into('<3I',boot_data,64,0x0c000000|(0x730844>>2),
                         0x08000000|(0x73084c>>2),0x0c000000|(0x800000>>2))
        boot=analysis_elf(boot_data,0x100040)
        declaration={'entry':'0x730840','roots':[],
                     'bootCallRanges':[['0x100040','0x10004c']]}
        module=inspect(fixture())
        self.assertEqual(module_roots(boot,module,declaration),[0x730840,0x730844,0x73084c])
        declaration['bootCallRanges']=[['0x200000','0x200008']]
        with self.assertRaises(ValueError):module_roots(boot,module,declaration)

    def test_boot_data_callback_table_seeds_code_without_touching_original_bytes(self):
        boot_data=fixture();struct.pack_into('<I',boot_data,8,0x100000)
        struct.pack_into('<4I',boot_data,96,0x730844,0x73085c,0,0x730860)
        boot=analysis_elf(boot_data,0x100040);original=bytes(boot)
        declaration={'entry':'0x730840','bootCallbackTables':[['0x100060','0x100070']]}
        module=inspect(fixture())
        self.assertEqual(module_roots(boot,module,declaration),[0x730840,0x730844,0x73085c])
        self.assertEqual(boot,original)
        declaration['bootCallbackTables']=[['0x200000','0x200004']]
        with self.assertRaises(ValueError):module_roots(boot,module,declaration)


if __name__ == '__main__':
    unittest.main()
