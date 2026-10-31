import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import ps2_disc as disc


def dual(value, width):
    return value.to_bytes(width, 'little') + value.to_bytes(width, 'big')


def record(name, lba, size, flags=0):
    result = bytearray(33+len(name)+(not len(name) % 2))
    result[0] = len(result)
    result[2:10] = dual(lba, 4)
    result[10:18] = dual(size, 4)
    result[25] = flags
    result[28:32] = dual(1, 2)
    result[32] = len(name)
    result[33:33+len(name)] = name
    return result


def fixture(boot='SLPS_253.52', extra=b''):
    image = bytearray(26 * disc.SECTOR)
    primary = memoryview(image)[16*disc.SECTOR:17*disc.SECTOR]
    primary[:7] = b'\x01CD001\x01'
    primary[40:72] = b'AUTHORED TEST'.ljust(32, b' ')
    primary[80:88] = dual(26, 4)
    primary[128:132] = dual(disc.SECTOR, 2)
    root = record(b'\0', 20, disc.SECTOR, 2)
    primary[156:156+len(root)] = root
    image[17*disc.SECTOR:17*disc.SECTOR+7] = b'\xffCD001\x01'
    system = f'BOOT2 = cdrom0:\\{boot};1\r\nVER = 1.00\r\n'.encode()
    elf = bytearray(136)
    elf[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HHIIIIIHHHHHH', elf, 16, 2, 8, 1, 0x100000, 52, 0,
                     0x20924000, 52, 32, 1, 0, 0, 0)
    struct.pack_into('<8I', elf, 52, 1, 128, 0x100000, 0x100000, 8, 8, 5, 16)
    elf[128:] = struct.pack('<II', 0x03e00008, 0)  # authored JR RA / delay-slot NOP
    listing = (root + record(b'\1', 20, disc.SECTOR, 2) +
               record(b'SYSTEM.CNF;1', 21, len(system)) +
               record(boot.encode()+b';1', 22, len(elf)) + extra)
    image[20*disc.SECTOR:20*disc.SECTOR+len(listing)] = listing
    image[21*disc.SECTOR:21*disc.SECTOR+len(system)] = system
    image[22*disc.SECTOR:22*disc.SECTOR+len(elf)] = elf
    return image


class Ps2DiscTests(unittest.TestCase):
    def test_boot_inventory_and_exclusive_extraction_preserve_iso(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = fixture()
            iso, output = root/'Original game.iso', root/'prepared/boot.elf'
            iso.write_bytes(image)
            result = disc.inspect(iso, output)
            self.assertEqual((result['serial'], result['candidateGame']), ('SLPS-25352', 'espgaluda'))
            self.assertEqual(result['elf']['entry'], '0x00100000')
            self.assertEqual(result['elf']['size'], 136)
            self.assertEqual(iso.read_bytes(), image)
            before = output.read_bytes()
            with self.assertRaises(FileExistsError):
                disc.inspect(iso, output)
            self.assertEqual(output.read_bytes(), before)
            with self.assertRaisesRegex(ValueError, 'overlaps'):
                disc.inspect(iso, iso)
            self.assertEqual(iso.read_bytes(), image)

    def test_ibara_serial_and_unrelated_disc_are_distinct(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)/'disc.iso'
            path.write_bytes(fixture('SLPM_663.01'))
            self.assertEqual(disc.inspect(path)['candidateGame'], 'ibara')
            path.write_bytes(fixture('SLPS_256.88'))
            self.assertIsNone(disc.inspect(path)['candidateGame'])

    def test_bounds_endian_copies_and_raw_sector_format(self):
        image = fixture()
        with self.assertRaisesRegex(ValueError, 'Truncated ISO'):
            disc.Iso9660(io.BytesIO(image[:-disc.SECTOR]), len(image)-disc.SECTOR)
        image[16*disc.SECTOR+84] ^= 1
        with self.assertRaisesRegex(ValueError, 'endian'):
            disc.Iso9660(io.BytesIO(image), len(image))
        with self.assertRaisesRegex(ValueError, 'ISO9660'):
            disc.Iso9660(io.BytesIO(bytes(26*disc.SECTOR)), 26*disc.SECTOR)

    def test_repeated_directory_and_duplicate_files_are_rejected(self):
        for extra in (record(b'LOOP', 20, disc.SECTOR, 2),
                      record(b'SYSTEM.CNF;2', 21, 1),
                      record(b'BAD', 9999, 1), record(b'EXTENT', 23, 1, 0x80)):
            image = fixture(extra=extra)
            with self.assertRaises(ValueError):
                disc.Iso9660(io.BytesIO(image), len(image)).entries()

    def test_boot_path_normalization_and_traversal(self):
        self.assertEqual(disc.boot_path(b'boot2 = CDROM0:\\dir\\slps_253.52;1\r\n'),
                         'DIR/SLPS_253.52')
        for path in (b'BOOT2=cdrom0:\\..\\secret', b'BOOT=cdrom0:\\SLPS_253.52',
                     b'BOOT2=cdrom0:\\D:\\secret'):
            with self.assertRaises(ValueError):
                disc.boot_path(path)

    def test_elf_segment_and_entry_must_be_backed(self):
        image = fixture()
        elf = bytearray(image[22*disc.SECTOR:22*disc.SECTOR+136])
        struct.pack_into('<I', elf, 24, 0x200000)
        with self.assertRaisesRegex(ValueError, 'entry point'):
            disc.elf_info(elf)
        struct.pack_into('<I', elf, 24, 0x100000)
        struct.pack_into('<I', elf, 52+16, 100)
        with self.assertRaisesRegex(ValueError, 'segment'):
            disc.elf_info(elf)


if __name__ == '__main__':
    unittest.main()
