import csv
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import ps2_ibara as ibara
import ps2_build_receipt as receipt


class IbaraIntegrationTests(unittest.TestCase):
    def test_reviewed_analysis_keeps_packet_builders_and_dispatch_callbacks(self):
        spec=ibara.metadata()
        rows=list(csv.DictReader(io.StringIO(ibara.read_map(ibara.METADATA.parent,spec).decode())))
        by_address={int(row['Start'],0):row for row in rows}
        for pc,name in ((0x1485d0,'sceGsSetDefLoadImage'),(0x1487b8,'sceGsSetDefStoreImage'),
                        (0x147ec8,'sceGsSetDefDBuff'),(0x148160,'sceGsSwapDBuff'),
                        (0x147de0,'sceGsPutDrawEnv'),(0x1475f0,'sceGsSetDefDispEnv')):
            self.assertEqual(by_address[pc]['Name'],'original_'+name)
            self.assertFalse(any(selector.startswith(name+'@') for selector in spec['general']['stubs']))
        for pc in (0x173460,0x1734d0,0x191820,0x1bc880):
            self.assertIn(pc,by_address)
        self.assertFalse(any(0x214000<=pc<0x215680 for pc in by_address))
        self.assertEqual(len(rows),spec['rows'])

    def test_metadata_rejects_private_paths_and_disabled_coverage(self):
        original=json.loads(ibara.METADATA.read_text())
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/original['map']).write_bytes((ibara.METADATA.parent/original['map']).read_bytes())
            manifest=root/'manifest.json'
            for key,value in (('input','private/boot.elf'),('require_static_edges',False),('require_sdk_map_entries',False)):
                spec=json.loads(json.dumps(original));spec['general'][key]=value
                manifest.write_text(json.dumps(spec))
                with self.subTest(key=key),self.assertRaises(ValueError):ibara.metadata(manifest)

    def test_receipt_game_selection_requires_matching_outputs(self):
        self.assertEqual(receipt.configured_games({'PS2_IBARA_GENERATED':'generated'}),['ibara'])
        self.assertEqual(receipt.configured_games({'PS2_GAME_GENERATED':'g1','PS2_IBARA_GENERATED':'ibara'}),['espgaluda','ibara'])
        self.assertEqual(set(receipt.output_names(['espgaluda','ibara'])),
                         {'espgaluda.exe','espgaluda.pdb','ibara.exe','ibara.pdb','SDL3.dll','SDL3.pdb'})
        with self.assertRaises(ValueError):receipt.configured_games({})
        for games in ([],['ibara','espgaluda'],['unknown']):
            with self.subTest(games=games),self.assertRaises(ValueError):receipt.output_names(games)


if __name__=='__main__':unittest.main()
