import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/'scripts/ps4'))
from server.games import garegga,feveron,ketsui
from replay_fixture import record
from test_esprade_community import score,upload


class GareggaCommunityTests(unittest.TestCase):
    def test_title_board_set_limits_and_independent_identity(self):
        self.assertEqual(set(garegga.BOARDS),{mode*10+ship for mode in range(4) for ship in range(9)})
        self.assertEqual(garegga.BOARDS[38]['ship'],'bornnam')
        self.assertEqual(garegga.BOARDS[20]['mode'],'premium')
        self.assertEqual((garegga.Store.title,garegga.Store.version),('CUSA10207','1.00'))
        self.assertNotEqual(garegga.account_id('Alice'),feveron.account_id('Alice'))
        for board,item in garegga.BOARDS.items():
            self.assertEqual(item['upload_limit'],100 if board%10==0 else 10)

    def test_all_native_modes_and_ships_preserve_uploaded_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'garegga.sqlite3';store=garegga.Store(path)
            for mode in range(4):
                for ship in range(9):
                    board=mode*10+ship;request=score(board);receipt=store.submit(request)
                    self.assertTrue(receipt['accepted'] and receipt['upload_eligible'])
                    raw=record('garegga')
                    struct.pack_into('<H',raw,106,mode)
                    struct.pack_into('<H',raw,112,(max(0,ship-1)<<4)|12)
                    payload,result=upload(store,request,receipt,raw)
                    self.assertEqual(store.download(receipt['entry_id'])[0],payload)
                    self.assertEqual(result['size'],len(payload))
                    self.assertFalse(store.submit(score(board,'12344'))['accepted'])
            self.assertEqual(garegga.Store(path).leaderboard(38,'standard')['total'],1)

    def test_five_original_record_versions_and_core_regions(self):
        for version in garegga.REPLAY_VERSIONS:
            for region in garegga.REGIONS:
                raw=record('garegga');struct.pack_into('<I',raw,0,version)
                raw[80:96]=region.ljust(16,b'\0')
                parsed=garegga.replay_info(zlib.compress(raw))
                self.assertEqual((parsed['version'],parsed['data_version']),(version,0x10000))
        for offset,fmt,value in ((0,'I',0x10001),(4,'I',0x10001),(112,'H',128),(104,'H',1)):
            raw=record('garegga');struct.pack_into('<'+fmt,raw,offset,value)
            with self.subTest(offset=offset),self.assertRaises(ketsui.ApiError):
                garegga.replay_info(zlib.compress(raw))

    def test_unknown_board_and_mismatched_attachment_are_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            store=garegga.Store(Path(temporary)/'limits.sqlite3')
            for board in (9,19,29,39,40,99):
                with self.assertRaises(ketsui.ApiError):store.submit(score(board))
            with self.assertRaises(ketsui.ApiError):store.submit(score(0,'4294967296'))
            for board,value in ((0,'12346'),(2,'12345'),(10,'12345')):
                request=score(board,value);receipt=store.submit(request)
                with self.assertRaises(ketsui.ApiError):upload(store,request,receipt,record('garegga'))
                self.assertIsNone(store.leaderboard(board,'standard')['entries'][0]['replay'])

    def test_peer_database_is_not_reinterpreted(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'peer.sqlite3';feveron.Store(path).submit(score(0))
            before=hashlib.sha256(path.read_bytes()).hexdigest()
            with self.assertRaises(RuntimeError):garegga.Store(path)
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),before)
