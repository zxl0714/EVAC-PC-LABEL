import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import ps4_inventory as module


def sfo(values):
    keys=bytearray();data=bytearray();entries=bytearray()
    for name,value in values.items():
        position=len(keys);keys+=name.encode()+b'\0'
        raw=value.encode()+b'\0'
        entries+=struct.pack('<HHIII',position,0x204,len(raw),len(raw),len(data));data+=raw
    start=20+len(entries)
    return struct.pack('<5I',0x46535000,0x101,start,start+len(keys),len(values))+entries+keys+data


def fixture(root):
    for folder,version,category in [('CUSA09062-app','01.00','gd'),('CUSA09062-patch','01.02','gp')]:
        directory=root/folder; (directory/'sce_sys').mkdir(parents=True)
        (directory/'sce_sys/param.sfo').write_bytes(sfo(dict(TITLE_ID='CUSA09062',APP_VER=version,CATEGORY=category)))
        (directory/'eboot.bin').write_bytes(b'authored executable placeholder')
    (root/'CUSA09062.complete').touch()


class Ps4InventoryTests(unittest.TestCase):
    def test_complete_metadata_only_and_optional_extraction_marker(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);fixture(root)
            before={path:path.read_bytes() for path in root.rglob('*') if path.is_file()}
            record=module.inventory(root)
            self.assertEqual(record['game'],'ketsui')
            self.assertEqual(len(record['files']),4)
            self.assertEqual(sum(item['program'] for item in record['files']),2)
            self.assertNotIn('authored executable',json.dumps(record))
            self.assertEqual(before,{path:path.read_bytes() for path in root.rglob('*') if path.is_file()})
            (root/'CUSA09062.complete').unlink()
            self.assertEqual(module.inventory(root),record)

    def test_wrong_title_patch_version_and_extra_root_data_are_rejected(self):
        for values in [dict(TITLE_ID='CUSA00001',APP_VER='01.02',CATEGORY='gp'),
                       dict(TITLE_ID='CUSA09062',APP_VER='01.01',CATEGORY='gp'),
                       dict(TITLE_ID='CUSA09062',APP_VER='01.02',CATEGORY='gd')]:
            with self.subTest(values=values), tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);fixture(root)
                (root/'CUSA09062-patch/sce_sys/param.sfo').write_bytes(sfo(values))
                with self.assertRaisesRegex(ValueError,'Unsupported'):module.inventory(root)
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);fixture(root);(root/'unexpected.dat').write_bytes(b'authored')
            with self.assertRaisesRegex(ValueError,'Expected only'):module.inventory(root)

    def test_bounded_sfo_rejects_truncation_bad_extents_and_duplicate_fields(self):
        valid=sfo(dict(TITLE_ID='CUSA09062',APP_VER='01.02'))
        cases=[valid[:12],b'bad!'+valid[4:]]
        extent=bytearray(valid);struct.pack_into('<I',extent,20+12,0x7fffffff);cases.append(extent)
        duplicate=bytearray(valid);struct.pack_into('<H',duplicate,36,0);cases.append(duplicate)
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'param.sfo'
            for raw in cases:
                path.write_bytes(raw)
                with self.assertRaises(ValueError):module.sfo(path)


if __name__=='__main__':unittest.main()
