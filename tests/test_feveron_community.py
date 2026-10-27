import hashlib
import http.client
import json
from pathlib import Path
import struct
import sys
import tempfile
import threading
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'scripts/ps4'))
from server.games import daioujou, esprade, feveron, ketsui
from replay_fixture import record
from test_esprade_community import score, upload


class FeveronCommunityTests(unittest.TestCase):
    def test_native_boards_limits_and_identity(self):
        self.assertEqual(set(feveron.BOARDS), {group * 10 + ship for group in range(8) for ship in range(5)})
        self.assertEqual(feveron.BOARDS[10]['mode'], 'arcade_time_attack')
        self.assertEqual(feveron.BOARDS[74]['mode'], 'custom_time_attack')
        self.assertEqual(feveron.BOARDS[4]['ship'], 'uotaro_poko')
        for board, item in feveron.BOARDS.items():
            self.assertEqual(item['upload_limit'], 100 if board < 60 and board % 10 == 0 else 10)
        self.assertEqual(len({c.account_id('Alice') for c in (ketsui.Store, daioujou.Store, esprade.Store, feveron.Store)}), 4)
        self.assertEqual((feveron.Store.title, feveron.Store.version), ('CUSA06735', '1.00'))

    def test_all_native_modes_and_packed_ships_roundtrip_without_rewriting(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'feveron.sqlite3'
            store = feveron.Store(path)
            for group in range(8):
                for ship in range(5):
                    board = group * 10 + ship
                    request = score(board)
                    receipt = store.submit(request)
                    self.assertTrue(receipt['accepted'] and receipt['upload_eligible'])
                    self.assertEqual(store.submit(request), receipt)
                    raw = record('feveron')
                    struct.pack_into('<H', raw, 106, group // 2)
                    packed = ((4 + max(0, ship - 1)) << 8) | 0x34
                    struct.pack_into('<H', raw, 112, packed)
                    payload, result = upload(store, request, receipt, raw)
                    self.assertEqual(result['size'], len(payload))
                    self.assertEqual(store.download(receipt['entry_id'])[0], payload)
                    self.assertEqual(store.leaderboard(board, 'standard', account=receipt['account_id'])['entries'][0]['score'], '12345')
                    self.assertFalse(store.submit(score(board, '12344'))['accepted'])
            reopened = feveron.Store(path)
            self.assertEqual(reopened.leaderboard(70, 'standard')['total'], 1)
            self.assertIsNotNone(reopened.leaderboard(70, 'standard')['entries'][0]['replay'])

    def test_original_container_core_and_packed_metadata(self):
        for version in feveron.REPLAY_VERSIONS:
            for region in feveron.REGIONS:
                raw = record('feveron')
                struct.pack_into('<I', raw, 0, version)
                raw[80:96] = region.ljust(16, b'\0')
                struct.pack_into('<H', raw, 112, 0x734)
                info = feveron.replay_info(zlib.compress(raw))
                self.assertEqual((info['version'], info['data_version'], info['character']), (version, 0x10000, 0x734))
        for offset, fmt, value in ((0, 'I', 0x10003), (4, 'I', 0x10001), (8, 'I', 0x78001),
                                   (104, 'H', 1), (112, 'H', 0x800), (112, 'H', 0x40), (112, 'H', 5)):
            raw = record('feveron')
            struct.pack_into('<' + fmt, raw, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ketsui.ApiError):
                feveron.replay_info(zlib.compress(raw))
        raw = record('feveron'); raw[80:96] = b'ARCADE_01030'.ljust(16, b'\0')
        with self.assertRaises(ketsui.ApiError):
            feveron.replay_info(zlib.compress(raw))
        with self.assertRaises(ketsui.ApiError):
            feveron.replay_info(zlib.compress(record('feveron')) + b'trailing')

    def test_unknown_boards_overflow_mode_ship_and_score_mismatch(self):
        with tempfile.TemporaryDirectory() as temporary:
            store = feveron.Store(Path(temporary) / 'limits.sqlite3')
            for board in (5, 15, 65, 75, 88, 99):
                with self.assertRaises(ketsui.ApiError):
                    store.submit(score(board))
            with self.assertRaises(ketsui.ApiError):
                store.submit(score(0, '4294967296'))
            for board, value in ((0, '12346'), (2, '12345'), (20, '12345'), (70, '12345')):
                request = score(board, value); receipt = store.submit(request)
                with self.assertRaises(ketsui.ApiError) as error:
                    upload(store, request, receipt, record('feveron'))
                self.assertEqual(error.exception.code, 'native_replay_mismatch')
                self.assertIsNone(store.leaderboard(board, 'standard')['entries'][0]['replay'])

    def test_peer_database_is_rejected_unchanged(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'peer.sqlite3'
            esprade.Store(path).submit(score(0))
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            with self.assertRaises(RuntimeError):
                feveron.Store(path)
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_http_title_health_empty_time_attack_and_peer_route(self):
        with tempfile.TemporaryDirectory() as temporary:
            server = ketsui.Server(0, feveron.Store(Path(temporary) / 'http.sqlite3'))
            thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': 0.01})
            thread.start()
            try:
                for path, status in ((feveron.Store.api + '/health', 200),
                                     (feveron.Store.api + '/leaderboards?board=70&rules=standard', 200),
                                     (esprade.Store.api + '/health', 404)):
                    connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=5)
                    connection.request('GET', path)
                    response = connection.getresponse(); body = json.loads(response.read())
                    self.assertEqual(response.status, status)
                    if path.endswith('/health') and status == 200:
                        self.assertEqual((body['title_id'], body['game_version']), ('CUSA06735', '1.00'))
                    connection.close()
            finally:
                server.shutdown(); server.server_close(); thread.join(5)
                self.assertFalse(thread.is_alive())


if __name__ == '__main__':
    unittest.main()
