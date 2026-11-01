import gzip
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import ps2_game


class Ps2GameTests(unittest.TestCase):
    def test_frozen_analysis_has_every_module_and_no_machine_paths(self):
        spec=ps2_game.load_metadata()
        self.assertEqual(len(spec['modules']),18)
        self.assertEqual(sum('extra' in module for module in spec['modules']),13)
        text=json.dumps(spec)
        self.assertNotIn('E:',text)
        self.assertNotIn('Xiaolu',text)

    def test_config_roundtrips_unicode_paths_and_boolean_settings(self):
        root=Path(tempfile.gettempdir())/'espgaluda authored fixture'
        general=dict(patch_cop0=True,stubs=['sdk@0x100000'],skip=[],output_worker_threads=2)
        text=ps2_game.config_text(general,root/'原始.elf',root/'generated',root/'functions.csv',root/'sdk.cpp')
        value=tomllib.loads(text)['general']
        self.assertTrue(value['patch_cop0'])
        self.assertEqual(value['input'],(root/'原始.elf').as_posix())
        self.assertEqual(value['stubs'],general['stubs'])

    def test_maps_reject_wrong_hash_count_columns_and_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            data=b'Name,Start,End,Size\nfixture,0x100000,0x100008,8\n'
            (root/'map.csv.gz').write_bytes(gzip.compress(data,mtime=0))
            spec=dict(map='map.csv.gz',mapSha256=hashlib.sha256(data).hexdigest(),rows=1)
            self.assertEqual(ps2_game.read_map(root,spec),data)
            for override in (dict(rows=2),dict(mapSha256='0'*64),dict(map='../map.csv.gz')):
                with self.subTest(override=override),self.assertRaises(ValueError):
                    ps2_game.read_map(root,dict(spec,**override))

    def test_iso_selection_checks_boot_identity_and_ambiguity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            first=root/'original name.iso';first.write_bytes(b'authored')
            other=root/'other game.iso';other.write_bytes(b'authored')
            expected=json.loads(ps2_game.METADATA.read_text())['bootSha256']
            def inspect(path):
                return dict(candidateGame='espgaluda' if path==first else 'ibara',elf=dict(sha256=expected))
            with patch.object(ps2_game,'inspect_disc',side_effect=inspect):
                self.assertEqual(ps2_game.find_iso(root),first)
            with patch.object(ps2_game,'inspect_disc',return_value=dict(candidateGame='espgaluda',elf=dict(sha256=expected))):
                with self.assertRaisesRegex(ValueError,'one supported'):
                    ps2_game.find_iso(root)


if __name__=='__main__':
    unittest.main()
