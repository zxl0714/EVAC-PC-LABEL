"""Authored community service data and actual loopback HTTP; no game assets."""
import base64
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing
import hashlib
import http.client
import json
from pathlib import Path
import socket
import sqlite3
import sys
import tempfile
import threading
import unittest
from unittest import mock
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'server'))
import ketsui_live as live


def score(name='Player A', value='100', board=0, rules='standard', **changes):
    result = dict(submission_id=uuid.uuid4().hex, nickname=name, board=board, rules=rules,
                  score=value, comment_base64=base64.b64encode(b'U1=1,\0' + bytes(58)).decode('ascii'),
                  game_info_base64='', build='authored-test')
    result.update(changes)
    return result


def replay(receipt, request, payload=b'authored opaque native attachment'):
    return dict(entry_id=receipt['entry_id'], submission_id=request['submission_id'],
                nickname=request['nickname'], format=live.REPLAY_FORMAT,
                sha256=hashlib.sha256(payload).hexdigest(),
                payload_base64=base64.b64encode(payload).decode('ascii'))


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'ketsui.sqlite3'
        self.store = live.Store(self.path)

    def failure(self, code, function, *args, status=400):
        with self.assertRaises(live.ApiError) as raised:
            function(*args)
        self.assertEqual((raised.exception.status, raised.exception.code), (status, code))

    def test_nickname_normalization_limits_and_internal_identity(self):
        self.assertEqual(live.nickname('  Ａlice  '), 'Alice')
        self.assertEqual(live.nickname('小鹿'), '小鹿')
        for value in ('', '  ', 'a\0b', 'a\nb', 'a\u200db', '\ud800', 'a' * 33,
                      '绊' * 17, '㍿' * 12, None, 123):
            with self.subTest(value=repr(value)):
                self.failure('invalid_nickname', live.nickname, value)
        self.assertEqual(len(live.nickname('绊' * 16).encode('utf-8')), 48)
        first = self.store.submit(score('Ａlice'))
        second = self.store.submit(score('Alice', '200'))
        self.assertEqual(first['account_id'], second['account_id'])
        self.assertGreater(int(first['account_id']), 0)
        self.assertLess(int(first['account_id']), 1 << 63)

    def test_known_boards_and_custom_partition_are_not_guessed(self):
        self.assertEqual(len(live.BOARDS), 22)
        self.assertEqual(live.BOARDS[51]['mode'], 'ikd_2007')
        self.assertEqual(live.BOARDS[51]['ship'], 'type_a')
        self.assertEqual(live.BOARDS[99]['upload_limit'], 0)
        for value in (3, -1, 100, True, '0'):
            with self.assertRaises(live.ApiError):
                self.store.submit(score(board=value))
        self.failure('custom_rules_required', self.store.submit, score(board=60))
        self.store.submit(score(board=60, rules='custom-' + 'a' * 64))
        self.assertEqual(self.store.leaderboard(60, 'custom-' + 'a' * 64)['total'], 1)
        self.assertEqual(self.store.leaderboard(60, 'custom-' + 'b' * 64)['total'], 0)

    def test_score_receipt_retries_are_idempotent_and_conflicts_do_not_mutate(self):
        request = score()
        receipt = self.store.submit(request)
        self.assertEqual(self.store.submit(dict(request)), receipt)
        self.failure('submission_conflict', self.store.submit, dict(request, score='101'), status=409)
        self.failure('submission_conflict', self.store.submit, dict(request, nickname='Other'), status=409)
        with self.store.connect() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM scores').fetchone()[0], 1)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM players').fetchone()[0], 1)
        self.assertTrue(receipt['accepted'])
        self.assertTrue(receipt['upload_eligible'])

    def test_ranking_best_per_player_ties_stable_and_lower_scores_rejected(self):
        first = self.store.submit(score('A', '100'))
        self.store.submit(score('B', '100'))
        equal = self.store.submit(score('A', '100'))
        lower = self.store.submit(score('A', '50'))
        self.assertFalse(equal['accepted'])
        self.assertFalse(lower['accepted'])
        self.assertFalse(lower['upload_eligible'])
        page = self.store.leaderboard(0, 'standard')
        self.assertEqual(page['total'], 2)
        self.assertEqual([e['nickname'] for e in page['entries']], ['A', 'B'])
        self.assertEqual([e['serial_rank'] for e in page['entries']], [1, 2])
        self.assertEqual([e['rank'] for e in page['entries']], [1, 1])
        self.assertEqual(page['entries'][0]['entry_id'], first['entry_id'])
        self.store.submit(score('B', '101'))
        page = self.store.leaderboard(0, 'standard', 2, 1)
        self.assertEqual(page['entries'][0]['nickname'], 'A')
        self.assertEqual(page['entries'][0]['rank'], 2)
        self.assertEqual(self.store.leaderboard(0, 'standard', 3)['entries'], [])

    def test_exact_signed_64_bit_values_and_canonical_decimal_strings(self):
        maximum = str((1 << 63) - 1)
        self.store.submit(score(value=maximum))
        self.assertEqual(self.store.leaderboard(0, 'standard')['entries'][0]['score'], maximum)
        for value in ('-1', '-0', '+1', '01', '1.0', ' 1', '1e9', str(1 << 63),
                      100, True, 1.25, None, '9' * 100):
            with self.subTest(value=value):
                self.failure('invalid_decimal', self.store.submit, score(value=value))

    def test_opaque_native_comment_and_game_info_round_trip(self):
        comment = bytes(range(64))
        info = bytes(range(189))
        self.store.submit(score(comment_base64=base64.b64encode(comment).decode(),
                                game_info_base64=base64.b64encode(info).decode()))
        entry = self.store.leaderboard(0, 'standard')['entries'][0]
        self.assertEqual(base64.b64decode(entry['comment_base64']), comment)
        self.assertEqual(base64.b64decode(entry['game_info_base64']), info)
        for raw in (bytes(63), bytes(65)):
            self.failure('invalid_binary', self.store.submit,
                         score(comment_base64=base64.b64encode(raw).decode()))
        self.failure('invalid_binary', self.store.submit,
                     score(game_info_base64=base64.b64encode(bytes(190)).decode()))
        self.failure('noncanonical_binary', live.binary, 'AB==', 2)
        self.failure('invalid_binary', live.binary, 'not base64', 20)

    def test_personal_query_unknown_account_and_board_rules_isolation(self):
        first = self.store.submit(score('A'))
        self.store.submit(score('B', '200'))
        self.store.submit(score('A', '999', board=1))
        self.store.submit(score('A', '888', rules='practice-test'))
        own = self.store.leaderboard(0, 'standard', account=first['account_id'])
        self.assertEqual(own['total'], 2)
        self.assertEqual([e['nickname'] for e in own['entries']], ['A'])
        self.assertEqual(own['entries'][0]['serial_rank'], 2)
        self.assertEqual(own['entries'][0]['score'], '100')
        self.assertEqual(self.store.leaderboard(0, 'standard', account='1')['entries'], [])

    def test_replay_binds_exact_receipt_and_never_replaces_new_best(self):
        request = score('A')
        first = self.store.submit(request)
        upload = replay(first, request)
        stored = self.store.upload(upload)
        self.assertEqual(self.store.upload(dict(upload)), stored)
        self.assertEqual(self.store.download(first['entry_id'])[0], base64.b64decode(upload['payload_base64']))
        second = self.store.submit(score('A', '200'))
        best = self.store.leaderboard(0, 'standard')['entries'][0]
        self.assertEqual(best['entry_id'], second['entry_id'])
        self.assertIsNone(best['replay'])
        self.assertEqual(self.store.submit(request), first)  # Original receipt remains immutable.
        self.assertEqual(self.store.download(first['entry_id'])[1], upload['sha256'])
        self.failure('submission_mismatch', self.store.upload, dict(upload, entry_id=second['entry_id']), status=409)
        self.failure('submission_mismatch', self.store.upload, dict(upload, nickname='B'), status=409)
        self.failure('replay_conflict', self.store.upload, replay(first, request, b'other attachment'), status=409)

    def test_replay_eligibility_uses_native_board_upload_limit(self):
        for number in range(10):
            self.store.submit(score(str(number), str(1000 - number), board=1))
        request = score('Eleventh', '1', board=1)
        receipt = self.store.submit(request)
        self.assertEqual(receipt['serial_rank'], 11)
        self.assertFalse(receipt['upload_eligible'])
        self.failure('replay_not_eligible', self.store.upload, replay(receipt, request), status=409)
        request = score('No attachment board', board=99)
        receipt = self.store.submit(request)
        self.failure('replay_not_eligible', self.store.upload, replay(receipt, request), status=409)
        request = score('No attachment board', '1', board=99)
        receipt = self.store.submit(request)
        self.assertFalse(receipt['accepted'])

    def test_replay_invalid_digest_format_empty_and_missing_entry(self):
        request = score()
        receipt = self.store.submit(request)
        upload = replay(receipt, request)
        self.failure('incompatible_replay', self.store.upload, dict(upload, format='futari-xstorage-v1'), status=409)
        self.failure('replay_digest_mismatch', self.store.upload, dict(upload, sha256='0' * 64))
        self.failure('replay_digest_mismatch', self.store.upload, replay(receipt, request, b''))
        self.failure('unknown_entry', self.store.upload, dict(upload, entry_id='0' * 32), status=404)
        self.failure('replay_not_found', self.store.download, receipt['entry_id'], status=404)

    def test_database_reopen_preserves_scores_identity_receipts_and_bytes(self):
        request = score('小鹿', '9007199254740993')
        receipt = self.store.submit(request)
        self.store.upload(replay(receipt, request))
        before = self.store.leaderboard(0, 'standard')
        reopened = live.Store(self.path)
        self.assertEqual(reopened.leaderboard(0, 'standard'), before)
        self.assertEqual(reopened.submit(request), receipt)
        self.assertEqual(reopened.download(receipt['entry_id']), self.store.download(receipt['entry_id']))
        with reopened.connect() as db:
            self.assertEqual(db.execute('PRAGMA integrity_check').fetchone()[0], 'ok')
            self.assertEqual(db.execute('PRAGMA foreign_key_check').fetchall(), [])

    def test_sort_date_is_snapshot_stable_and_changes_only_for_accepted_scores(self):
        self.assertEqual(self.store.leaderboard(0, 'standard')['sorted_us'], '0')
        with mock.patch.object(live.time, 'time_ns', return_value=1000000000):
            self.store.submit(score(value='100'))
        before = self.store.leaderboard(0, 'standard')
        self.assertEqual(before['sorted_us'], '1000000')
        with mock.patch.object(live.time, 'time_ns', return_value=2000000000):
            self.store.submit(score(value='42'))
        self.assertEqual(self.store.leaderboard(0, 'standard'), before)
        with mock.patch.object(live.time, 'time_ns', return_value=3000000000):
            self.store.submit(score(value='200'))
        self.assertEqual(self.store.leaderboard(0, 'standard')['sorted_us'], '3000000')

    def test_capacity_limits_are_atomic_and_retries_still_work(self):
        limited = live.Store(self.path, max_scores=1, max_replay_storage=4)
        request = score()
        receipt = limited.submit(request)
        self.failure('score_capacity', limited.submit, score('B'), status=507)
        self.assertEqual(limited.submit(request), receipt)
        limited.upload(replay(receipt, request, b'abcd'))
        limited.upload(replay(receipt, request, b'abcd'))
        request2 = score('B')
        receipt2 = self.store.submit(request2)
        self.failure('replay_capacity', limited.upload, replay(receipt2, request2, b'x'), status=507)
        self.failure('replay_not_found', limited.download, receipt2['entry_id'], status=404)

    def test_concurrent_same_submission_and_different_high_scores(self):
        request = score()
        with ThreadPoolExecutor(max_workers=8) as executor:
            receipts = list(executor.map(lambda _: self.store.submit(request), range(8)))
        self.assertTrue(all(receipt == receipts[0] for receipt in receipts))
        with ThreadPoolExecutor(max_workers=4) as executor:
            list(executor.map(lambda i: self.store.submit(score('Player A', str(i))), range(101, 109)))
        self.assertEqual(self.store.leaderboard(0, 'standard')['entries'][0]['score'], '108')
        with self.store.connect() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM scores').fetchone()[0], 9)

    def test_unknown_database_and_future_schema_are_refused_without_changes(self):
        other = Path(self.directory.name) / 'futari.sqlite3'
        with closing(sqlite3.connect(other)) as db:
            db.execute('CREATE TABLE scores(secret TEXT)')
            db.execute("INSERT INTO scores VALUES('preserve')")
            db.commit()
        before = other.read_bytes()
        with self.assertRaises(RuntimeError):
            live.Store(other)
        self.assertEqual(other.read_bytes(), before)
        with self.store.connect(write=True) as db:
            db.execute('UPDATE ketsui_meta SET version=2')
        before = self.path.read_bytes()
        with self.assertRaises(RuntimeError):
            live.Store(self.path)
        self.assertEqual(self.path.read_bytes(), before)

    def test_account_mapping_collision_is_refused_and_transaction_rolled_back(self):
        first = self.store.submit(score('A'))
        with mock.patch.object(live, 'account_id', return_value=first['account_id']):
            self.failure('account_mapping_collision', self.store.submit, score('B'), status=409)
        with self.store.connect() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM players').fetchone()[0], 1)

    def test_corrupt_stored_replay_is_not_served(self):
        request = score()
        receipt = self.store.submit(request)
        self.store.upload(replay(receipt, request))
        with self.store.connect(write=True) as db:
            db.execute('UPDATE replays SET payload=?', (b'corrupt',))
        self.failure('stored_replay_corrupt', self.store.download, receipt['entry_id'], status=500)

    def test_strict_json_unknown_fields_and_build_rejection(self):
        for raw in (b'{"score":1,"score":2}', b'{"value":NaN}', b'\xff', b'{'):
            with self.assertRaises(live.ApiError):
                live.strict_json(raw)
        self.failure('invalid_fields', self.store.submit, dict(score(), extra=True))
        for build in ('', '../path', 'x\ny', 'x' * 81, None):
            self.failure('invalid_build', self.store.submit, score(build=build))


class HttpTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.store = live.Store(Path(self.directory.name) / 'http.sqlite3')
        self.server = live.Server(0, self.store)
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={'poll_interval': 0.01})
        self.thread.start()
        self.addCleanup(self.close_server)

    def close_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(3)
        self.assertFalse(self.thread.is_alive())

    def request(self, method, path, value=None, headers=None):
        body = None if value is None else json.dumps(value).encode('utf-8')
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
        try:
            connection.request(method, live.API + path, body, headers or ({'Content-Type': 'application/json'} if body is not None else {}))
            response = connection.getresponse()
            raw = response.read()
            content_type = response.getheader('Content-Type', '')
            return response.status, (json.loads(raw) if content_type.startswith('application/json') else raw), dict(response.getheaders())
        finally:
            connection.close()

    def test_actual_http_health_boards_score_rank_personal_and_replay(self):
        status, health, _ = self.request('GET', '/health')
        self.assertEqual(status, 200)
        self.assertEqual((health['title_id'], health['game_version']), ('CUSA09062', '1.02'))
        self.assertEqual(len(self.request('GET', '/boards')[1]['boards']), 22)
        request = score('小鹿', '9007199254740993')
        status, receipt, _ = self.request('POST', '/scores', request)
        self.assertEqual(status, 200)
        status, page, headers = self.request('GET', '/leaderboards?board=0&rules=standard&start=1&count=20')
        self.assertEqual((status, page['total']), (200, 1))
        self.assertEqual(page['entries'][0]['score'], request['score'])
        self.assertEqual(headers['Cache-Control'], 'no-store')
        personal = self.request('GET', '/leaderboards?board=0&rules=standard&account_id=' + receipt['account_id'])[1]
        self.assertEqual(personal['entries'], page['entries'])
        upload = replay(receipt, request)
        self.assertEqual(self.request('POST', '/replays', upload)[0], 200)
        status, payload, headers = self.request('GET', '/entries/' + receipt['entry_id'] + '/replay')
        self.assertEqual(status, 200)
        self.assertEqual(payload, base64.b64decode(upload['payload_base64']))
        self.assertEqual(headers['X-Replay-SHA256'], upload['sha256'])
        self.assertEqual(headers['X-Replay-Format'], live.REPLAY_FORMAT)

    def test_identity_normalizes_without_registering_or_creating_player_rows(self):
        status, identity, _ = self.request('POST', '/identity', {'nickname': '  Ａlice  '})
        self.assertEqual(status, 200)
        self.assertEqual(identity, {'nickname': 'Alice', 'account_id': live.account_id('Alice')})
        with self.store.connect() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM players').fetchone()[0], 0)
        self.assertEqual(self.request('POST', '/identity', {'nickname': '\u200d'})[0], 400)

    def test_empty_query_routes_work_with_python_310_strict_parser_semantics(self):
        original = live.parse_qs
        def strict_310(query, **options):
            if not query:
                raise ValueError('bad query field: empty string')
            return original(query, **options)
        with mock.patch.object(live, 'parse_qs', side_effect=strict_310) as parser:
            self.assertEqual(self.request('GET', '/health')[0], 200)
            self.assertEqual(self.request('GET', '/boards')[0], 200)
            self.assertEqual(self.request('POST', '/identity', {'nickname': 'Authored'})[0], 200)
            self.assertEqual(self.request('POST', '/scores', score())[0], 200)
            self.assertEqual(parser.call_count, 0)
            self.assertEqual(self.request('GET', '/leaderboards?board=0&rules=standard')[0], 200)
            self.assertEqual(self.request('GET', '/leaderboards?&')[0], 400)
            self.assertEqual(parser.call_count, 2)

    def test_invalid_routes_duplicate_queries_bools_and_browser_requests(self):
        for path in ('/no-such-route', '/entries/not-an-id/replay'):
            self.assertEqual(self.request('GET', path)[0], 404)
        for query in ('?board=0&board=1&rules=standard', '?board=0&rules=',
                      '?board=0&rules=standard&count=1', '?board=true&rules=standard',
                      '?board=0&rules=standard&start=0&count=20', '?board=0&rules=standard&x=y'):
            with self.subTest(query=query):
                self.assertEqual(self.request('GET', '/leaderboards' + query)[0], 400)
        self.assertEqual(self.request('POST', '/scores', score(), {'Origin': 'https://example.invalid', 'Content-Type': 'application/json'})[0], 403)
        self.assertEqual(self.request('POST', '/scores', score(), {'Content-Type': 'text/plain'})[0], 415)

    def test_request_framing_rejects_duplicates_chunked_oversized_and_bad_json(self):
        for extra, expected in (
            ([('Content-Length', '1'), ('Content-Length', '1')], 400),
            ([('Transfer-Encoding', 'chunked')], 400),
            ([('Content-Length', str(live.MAX_BODY + 1))], 413),
            ([('Content-Length', '-1')], 400),
        ):
            with self.subTest(extra=extra):
                connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
                try:
                    connection.putrequest('POST', live.API + '/scores')
                    connection.putheader('Content-Type', 'application/json')
                    for key, value in extra:
                        connection.putheader(key, value)
                    connection.endheaders()
                    response = connection.getresponse()
                    self.assertEqual(response.status, expected)
                    response.read()
                finally:
                    connection.close()
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
        try:
            connection.request('POST', live.API + '/scores', b'{"a":1,"a":2}', {'Content-Type': 'application/json'})
            response = connection.getresponse()
            self.assertEqual(response.status, 400)
            self.assertEqual(json.loads(response.read())['error'], 'duplicate_field')
        finally:
            connection.close()

    def test_connection_budget_returns_503_and_recovers_after_disconnect(self):
        entered = threading.Event()
        class ObservedServer(live.Server):
            def process_request_thread(self, request, address):
                entered.set()
                super().process_request_thread(request, address)
        server = ObservedServer(0, self.store, max_connections=1)
        thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': 0.01})
        thread.start()
        first = socket.create_connection(('127.0.0.1', server.server_port), timeout=3)
        second = None
        try:
            first.sendall(b'POST /ketsui/api/v1/scores HTTP/1.0\r\nContent-Type: application/json\r\nContent-Length: 100\r\n\r\nx')
            self.assertTrue(entered.wait(2))
            second = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=3)
            second.request('GET', live.API + '/health')
            response = second.getresponse()
            self.assertEqual(response.status, 503)
            response.read()
        finally:
            first.close()
            if second:
                second.close()
            server.shutdown()
            server.server_close()
            thread.join(3)
        self.assertFalse(thread.is_alive())
        self.assertTrue(server.slots.acquire(blocking=False))
        server.slots.release()


if __name__ == '__main__':
    unittest.main()
