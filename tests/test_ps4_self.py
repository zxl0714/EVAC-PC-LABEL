import struct
import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts/ps4'))
from native_elf import Elf,plain_self_elf,nid
from ps4_elf_fixtures import libc,plain_self,cases


class PlainSelfTests(unittest.TestCase):
    def test_plain_segments_preserve_ret_and_original_input(self):
        source=plain_self(libc())
        before=bytes(source)
        decoded=plain_self_elf(source)
        elf=Elf(decoded)
        symbol=next(s for s in elf.symbols if s['name'].split('#')[0]==nid('_init_env'))
        self.assertEqual(symbol['size'],1)
        self.assertEqual(decoded[0x220],0xc3)
        self.assertEqual(source,before)
        self.assertEqual(plain_self_elf(libc()),libc())

    def test_rejects_encryption_compression_bad_mapping_and_truncation(self):
        for name,_,_,contract,valid in cases():
            if name.startswith('self-'):
                with self.subTest(name=name),self.assertRaises(ValueError):
                    plain_self_elf(contract)

    def test_missing_segment_and_conflicting_header_are_rejected(self):
        for offset,value in ((32,0),(32+8,0),(12,0)):
            raw=plain_self(libc())
            struct.pack_into('<Q',raw,offset,value)
            with self.subTest(offset=offset),self.assertRaises(ValueError):
                plain_self_elf(raw)

    def test_only_exact_version_note_tail_is_accepted(self):
        raw=plain_self(libc(),True)
        self.assertEqual(plain_self_elf(raw)[0x700:0x703],b'\x02v1')
        for source in (raw+b'x',plain_self(libc())+b'x'):
            with self.assertRaises(ValueError):plain_self_elf(source)


if __name__=='__main__':unittest.main()
