import base64
from contextlib import closing
import hashlib
import http.client
import json
from pathlib import Path
import sqlite3
import struct
import sys
import tempfile
import threading
import unittest
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'scripts/ps4'))
from server.games import daioujou, esprade, ketsui
from replay_fixture import record


def score(board, value='12345', nickname='Alice'):
    return dict(submission_id=uuid.uuid4().hex, nickname=nickname, board=board, rules='standard',
                score=value, comment_base64=base64.b64encode(bytes(64)).decode(),
                game_info_base64='', build='authored-esprade-test')


def upload(store, request, receipt, raw):
    payload = zlib.compress(raw)
    result = store.upload(dict(entry_id=receipt['entry_id'], submission_id=request['submission_id'],
        nickname=request['nickname'], format=store.replay_format, sha256=hashlib.sha256(payload).hexdigest(),
        payload_base64=base64.b64encode(payload).decode()))
    return payload, result


class EspradeCommunityTests(unittest.TestCase):
    def test_exact_native_boards_and_isolated_identity(self):
        self.assertEqual(set(esprade.BOARDS), {base + ship for base in (0, 10, 20, 30) for ship in range(5)} | {88, 99})
        self.assertEqual(esprade.BOARDS[14]['ship'], 'alice')
        self.assertEqual(esprade.BOARDS[30]['mode'], 'custom')
        for board in esprade.BOARDS:
            self.assertEqual(esprade.BOARDS[board]['upload_limit'], 0 if board >= 88 else 100 if board % 10 == 0 else 10)
        names = {cls.account_id('Alice') for cls in (ketsui.Store, daioujou.Store, esprade.Store)}
        self.assertEqual(len(names), 3)
        self.assertTrue(0 < int(esprade.account_id('Alice')) < 1 << 63)

    def test_all_modes_and_ship_boards_keep_original_score_and_replay(self):
        with tempfile.TemporaryDirectory() as temporary:
            store = esprade.Store(Path(temporary) / 'esprade.sqlite3')
            for mode in range(4):
                for ship in range(5):
                    board = mode * 10 + ship
                    self.assertEqual(store.leaderboard(board, 'standard')['total'], 0)
                    request = score(board)
                    receipt = store.submit(request)
                    self.assertEqual(store.submit(request), receipt)
                    self.assertTrue(receipt['accepted'] and receipt['upload_eligible'])
                    raw = record('esprade')
                    struct.pack_into('<H', raw, 106, mode)
                    struct.pack_into('<H', raw, 112, 4 + max(0, ship - 1))  # Route C, same native ship category.
                    payload, result = upload(store, request, receipt, raw)
                    self.assertEqual(result['size'], len(payload))
                    self.assertEqual(store.download(receipt['entry_id'])[0], payload)
                    row = store.leaderboard(board, 'standard', account=receipt['account_id'])['entries'][0]
                    self.assertEqual(row['score'], '12345')
                    self.assertIsNotNone(row['replay'])
                    self.assertFalse(store.submit(score(board, '12344'))['accepted'])
            self.assertEqual(store.leaderboard(88, 'standard')['total'], 0)

    def test_progress_boards_are_score_only_and_persistent(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'progress.sqlite3'
            store = esprade.Store(path)
            for board in (88, 99):
                first = store.submit(score(board, '0'))
                self.assertTrue(first['accepted'])
                self.assertFalse(first['upload_eligible'])
                request = score(board, '10003')
                receipt = store.submit(request)
                self.assertTrue(receipt['accepted'])
                self.assertFalse(receipt['upload_eligible'])
                with self.assertRaises(ketsui.ApiError) as error:
                    upload(store, request, receipt, record('esprade'))
                self.assertEqual(error.exception.code, 'replay_not_eligible')
            reopened = esprade.Store(path)
            for board in (88, 99):
                row = reopened.leaderboard(board, 'standard')['entries'][0]
                self.assertEqual(row['score'], '10003')
                self.assertIsNone(row['replay'])

    def test_invalid_board_rules_and_out_of_native_field_score(self):
        with tempfile.TemporaryDirectory() as temporary:
            store = esprade.Store(Path(temporary) / 'limits.sqlite3')
            for board in (5, 9, 15, 25, 35, 40, 87, 89, 98, 100):
                with self.subTest(board=board), self.assertRaises(ketsui.ApiError):
                    store.submit(score(board))
            for value in ('-1', '4294967296', '1.5'):
                with self.assertRaises(ketsui.ApiError):
                    store.submit(score(0, value))
            self.assertTrue(store.submit(score(0, '4294967295'))['accepted'])
            with self.assertRaises(ketsui.ApiError):
                store.submit(dict(score(30), rules='custom-' + 'a' * 64))

    def test_known_record_versions_and_core_revisions(self):
        for version in esprade.REPLAY_VERSIONS:
            for region in esprade.REGIONS:
                raw = record('esprade')
                struct.pack_into('<I', raw, 0, version)
                raw[80:96] = region.ljust(16, b'\0')
                info = esprade.replay_info(zlib.compress(raw))
                self.assertEqual((info['version'], info['region']), (version, region.decode()))

    def test_peer_records_and_invalid_metadata_are_refused(self):
        bad = [record('daioujou'), record('ketsui')]
        for offset, fmt, value in ((0, 'I', 0x10023), (4, 'I', 0x10001), (8, 'I', 0x78001),
                                   (104, 'H', 1), (112, 'H', 8), (16, 'I', 513)):
            raw = record('esprade')
            struct.pack_into('<' + fmt, raw, offset, value)
            bad.append(raw)
        bad.append(record('esprade')[:0x60000])
        for raw in bad:
            with self.assertRaises(ketsui.ApiError):
                esprade.replay_info(zlib.compress(raw))
        with self.assertRaises(ketsui.ApiError):
            esprade.replay_info(zlib.compress(record('esprade')) + b'trailing')

    def test_score_mode_and_ship_mismatch_do_not_attach(self):
        with tempfile.TemporaryDirectory() as temporary:
            store = esprade.Store(Path(temporary) / 'mismatch.sqlite3')
            for board, value in ((0, '12346'), (2, '12345'), (10, '12345'), (30, '12345')):
                request = score(board, value)
                receipt = store.submit(request)
                with self.assertRaises(ketsui.ApiError) as error:
                    upload(store, request, receipt, record('esprade'))
                self.assertEqual(error.exception.code, 'native_replay_mismatch')
                self.assertIsNone(store.leaderboard(board, 'standard')['entries'][0]['replay'])

    def test_older_attachment_remains_bound_after_new_best(self):
        with tempfile.TemporaryDirectory() as temporary:
            store = esprade.Store(Path(temporary) / 'receipt.sqlite3')
            request = score(0)
            first = store.submit(request)
            payload, _ = upload(store, request, first, record('esprade'))
            latest = store.submit(score(0, '12346'))
            self.assertIsNone(store.leaderboard(0, 'standard')['entries'][0]['replay'])
            self.assertEqual(store.download(first['entry_id'])[0], payload)
            self.assertNotEqual(first['entry_id'], latest['entry_id'])

    def test_peer_database_refused_without_changes(self):
        with tempfile.TemporaryDirectory() as temporary:
            for peer in (ketsui.Store, daioujou.Store):
                path = Path(temporary) / (peer.title + '.sqlite3')
                peer(path).submit(score(0))
                before = hashlib.sha256(path.read_bytes()).hexdigest()
                with self.assertRaises(RuntimeError):
                    esprade.Store(path)
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_http_health_empty_page_and_peer_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            server = ketsui.Server(0, esprade.Store(Path(temporary) / 'http.sqlite3'))
            thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': 0.01})
            thread.start()
            try:
                for path, status in ((esprade.Store.api + '/health', 200),
                                     (esprade.Store.api + '/leaderboards?board=88&rules=standard', 200),
                                     (daioujou.Store.api + '/health', 404)):
                    connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=5)
                    connection.request('GET', path)
                    response = connection.getresponse()
                    body = json.loads(response.read())
                    self.assertEqual(response.status, status)
                    if path.endswith('/health') and status == 200:
                        self.assertEqual((body['title_id'], body['game_version']), ('CUSA15155', '1.04'))
                    connection.close()
            finally:
                server.shutdown()
                server.server_close()
                thread.join(5)
                self.assertFalse(thread.is_alive())


if __name__ == '__main__':
    unittest.main()
