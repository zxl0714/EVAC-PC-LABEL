import copy
import base64
import hashlib
import http.client
import json
from pathlib import Path
import sys
import tempfile
import struct
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server.games.espgaluda2 import ApiError, Game, Server, Store, REPLAY_FORMAT


def submission(name='Player', score=9007199254740993, view=71, token='1' * 32):
    return dict(protocol=1, title_id='435A07D8', submission=token, nickname=name,
                view=view, rules='standard', score=str(score), build='authored-test',
                columns=[dict(id=0x20000001, type=2, value=str(score)),
                         dict(id=0x20000002, type=2, value='9007199254740001'),
                         dict(id=0x10000008, type=1, value=2)])


def replay_fixture(score=999):
    record = bytearray(136)
    struct.pack_into('>IIQQI', record, 0, 0, 2, 134368194753020000, score, 12)
    struct.pack_into('>I', record, 36, 2)
    struct.pack_into('>I', record, 96, 32)
    record[104:] = bytes(range(32))
    return bytes(record)


def replay_body(score_id, record=None):
    record = replay_fixture() if record is None else record
    return dict(protocol=1, title_id='435A07D8', score_id=score_id, nickname='Player',
                format=REPLAY_FORMAT, sha256=hashlib.sha256(record).hexdigest(),
                data=base64.b64encode(record).decode('ascii'))


class CommunityServerTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.path = Path(self.folder.name) / 'test.sqlite3'
        self.store = Store(self.path)

    def tearDown(self):
        self.folder.cleanup()

    def reject(self, body, status=400):
        with self.assertRaises(ApiError) as result:
            self.store.submit(body)
        self.assertEqual(result.exception.status, status)

    def replay_score(self, view=71, token='1' * 32):
        body = submission(score=999, view=view, token=token)
        body['columns'].extend([dict(id=0x20000011, type=2, value='134368194753020000'),
                                dict(id=0x10008011, type=1, value=136)])
        return self.store.submit(body)['id']

    def reject_replay(self, body, status=400):
        with self.assertRaises(ApiError) as caught:
            self.store.attach_replay(body)
        self.assertEqual(caught.exception.status, status)

    def test_64bit_precision_and_full_property_namespace(self):
        self.store.submit(submission())
        row = self.store.leaderboard(71)['rows'][0]
        self.assertEqual(row['score'], '9007199254740993')
        self.assertEqual(next(c['value'] for c in row['columns'] if c['id'] == 0x20000002), '9007199254740001')
        self.assertIsNone(row['replay_id'])

    def test_idempotent_submission_and_conflict(self):
        body = submission()
        first = self.store.submit(body)
        second = self.store.submit(copy.deepcopy(body))
        self.assertEqual(first['id'], second['id'])
        self.assertTrue(second['duplicate'])
        body['nickname'] = 'SomeoneElse'
        self.reject(body, 409)

    def test_nickname_normalization_without_registration(self):
        self.store.submit(submission(' Ｐｌａｙｅｒ '))
        self.assertEqual(self.store.leaderboard(71)['rows'][0]['nickname'], 'Player')

    def test_personal_best_and_tie_order(self):
        self.store.submit(submission('First', 100, token='1' * 32))
        self.store.submit(submission('Second', 100, token='2' * 32))
        self.store.submit(submission('First', 99, token='3' * 32))
        board = self.store.leaderboard(71)
        self.assertEqual(board['total'], 2)
        self.assertEqual([(r['nickname'], r['rank'], r['score']) for r in board['rows']],
                         [('First', 1, '100'), ('Second', 2, '100')])

    def test_view_isolation_paging_and_explicit_users(self):
        self.store.submit(submission('One', 300))
        self.store.submit(submission('Two', 200, token='2' * 32))
        self.store.submit(submission('OtherView', 999, view=72, token='3' * 32))
        self.assertEqual(self.store.leaderboard(71, start=2, count=1)['rows'][0]['nickname'], 'Two')
        self.assertEqual(self.store.leaderboard(71, names=['Two'])['rows'][0]['rank'], 2)
        self.assertEqual(self.store.leaderboard(71, names=['Missing'])['rows'], [])
        self.assertEqual(self.store.leaderboard(71, around='Two', count=1)['start'], 2)

    def test_estimate_excludes_old_personal_best_and_preserves_ties(self):
        self.store.submit(submission('One', 300))
        self.store.submit(submission('Two', 200, token='2' * 32))
        self.assertEqual(self.store.estimate(71, 'standard', '200', 'One')['rank'], 2)
        self.assertEqual(self.store.estimate(71, 'standard', '201', 'One')['rank'], 1)

    def test_no_zero_placeholder_on_first_lookup(self):
        self.assertEqual(self.store.leaderboard(71, names=['Player'])['rows'], [])
        self.assertEqual(self.store.boards()['boards'], [])

    def test_wrong_title_protocol_views_and_rules_rejected(self):
        for key, value, status in [('title_id', '435A07D7', 409), ('protocol', True, 409),
                                   ('view', 0, 400), ('view', 85, 400), ('rules', 'modified', 400)]:
            body = submission()
            body[key] = value
            with self.subTest(key=key, value=value):
                self.reject(body, status)

    def test_score_type_bounds_and_native_total_consistency(self):
        for score in (True, 9007199254740993, '-1', '9223372036854775808', '1.5', '1e5'):
            body = submission()
            body['score'] = score
            self.reject(body)
        body = submission()
        body['columns'][0]['value'] = '1'
        self.reject(body)

    def test_bad_columns_nicknames_and_size_bounds(self):
        for name in ('', 'X' * 49, 'a\u202eb', '\n'):
            self.reject(submission(name))
        body = submission()
        body['columns'].append(body['columns'][0].copy())
        self.reject(body)
        body = submission()
        body['columns'][1]['type'] = 4
        self.reject(body)

    def test_database_reopen_and_game_identity(self):
        self.store.submit(submission())
        self.assertEqual(Store(self.path).leaderboard(71)['total'], 1)
        with self.assertRaises(RuntimeError):
            Store(self.path, Game('other', '435A07D7', range(1, 85)))

    def test_replay_score_first_roundtrip_dedup_and_reopen(self):
        first = self.replay_score()
        second = self.replay_score(view=72, token='2' * 32)
        self.assertIsNone(self.store.leaderboard(71)['rows'][0]['replay_id'])
        attached = self.store.attach_replay(replay_body(first))
        self.assertEqual(self.store.attach_replay(replay_body(second))['id'], attached['id'])
        self.assertTrue(self.store.attach_replay(replay_body(first))['duplicate'])
        row = self.store.leaderboard(71)['rows'][0]
        self.assertEqual((row['replay_size'], row['replay_format'], row['replay_id']), (136, REPLAY_FORMAT, attached['id']))
        response = Store(self.path).replay(attached['id'])
        self.assertEqual(base64.b64decode(response['data']), replay_fixture())
        with self.store.connect() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM replays').fetchone()[0], 1)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM attachments').fetchone()[0], 2)

    def test_replay_wrong_run_owner_size_and_title(self):
        score_id = self.replay_score()
        self.reject_replay(replay_body('0' * 32), 404)
        for key, value, status in [('nickname', 'Other', 409), ('title_id', '435A07D7', 409),
                                   ('format', 'futari-xstorage-v1', 400), ('sha256', '0' * 64, 400)]:
            body = replay_body(score_id)
            body[key] = value
            with self.subTest(key=key):
                self.reject_replay(body, status)
        self.reject_replay(replay_body(score_id, replay_fixture(1000)), 409)
        for offset in (8, 36):
            data = bytearray(replay_fixture())
            data[offset + (7 if offset == 8 else 3)] ^= 1
            self.reject_replay(replay_body(score_id, data), 409 if offset == 8 else 400)

    def test_replay_envelope_encoding_and_attachment_conflict(self):
        score_id = self.replay_score()
        for offset, value in [(7, 3), (99, 33)]:
            data = bytearray(replay_fixture())
            data[offset] = value
            self.reject_replay(replay_body(score_id, data))
        self.reject_replay(replay_body(score_id, replay_fixture()[:104]))
        bad = replay_body(score_id)
        bad['data'] += '\n'
        self.reject_replay(bad)
        self.store.attach_replay(replay_body(score_id))
        changed = bytearray(replay_fixture())
        changed[-1] ^= 1
        self.reject_replay(replay_body(score_id, changed), 409)

    def test_replay_not_inherited_by_new_best_without_attachment(self):
        self.store.attach_replay(replay_body(self.replay_score()))
        self.store.submit(submission(score=1000, token='2' * 32))
        self.assertIsNone(self.store.leaderboard(71)['rows'][0]['replay_id'])

    def test_replay_requires_native_upload_request(self):
        score_id = self.store.submit(submission(score=999))['id']
        self.reject_replay(replay_body(score_id), 409)

    def test_native_network_zero_and_local_checksum_are_not_lengths(self):
        first = self.replay_score()
        self.store.attach_replay(replay_body(first))
        local = bytearray(replay_fixture())
        struct.pack_into('>I', local, 0, 0xffffffff)
        second = self.replay_score(view=72, token='2' * 32)
        self.store.attach_replay(replay_body(second, local))
        self.assertEqual(self.store.leaderboard(72)['rows'][0]['replay_size'], len(local))

    def test_http_namespace_health_and_bad_requests(self):
        server = Server(('127.0.0.1', 0), self.store)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            def request(path, method='GET', body=None):
                connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=3)
                try:
                    connection.request(method, path, body=body, headers={'Content-Type': 'application/json'})
                    response = connection.getresponse()
                    return response.status, json.loads(response.read())
                finally:
                    connection.close()
            prefix = server.prefix
            self.assertEqual(prefix, '/cave/api/v1/games/espgaluda2')
            status, body = request(prefix + '/health')
            self.assertEqual(status, 200)
            self.assertEqual(body['title_id'], '435A07D8')
            self.assertIn('replays', body['features'])
            self.assertEqual(request('/futari/api/v1/health')[0], 404)
            self.assertEqual(request('/cave/api/v1/games/ketsui/health')[0], 404)
            self.assertEqual(request(prefix + '/leaderboard?view=71&view=72')[0], 400)
            self.assertEqual(request(prefix + '/scores', 'POST', '{bad json')[0], 400)
            self.assertEqual(request(prefix + '/replays', 'POST', '{}')[0], 409)
            self.assertEqual(request(prefix + '/scores', 'POST', json.dumps(submission()))[0], 200)
            self.assertEqual(request(prefix + '/leaderboard?view=71')[1]['rows'][0]['score'], '9007199254740993')
            replay_score = self.replay_score(view=72, token='2' * 32)
            status, body = request(prefix + '/replays', 'POST', json.dumps(replay_body(replay_score)))
            self.assertEqual(status, 200)
            downloaded = request(prefix + '/replays/' + body['id'])
            self.assertEqual(downloaded[0], 200)
            self.assertEqual(base64.b64decode(downloaded[1]['data']), replay_fixture())
        finally:
            server.shutdown()
            server.server_close()
            worker.join(2)


if __name__ == '__main__':
    unittest.main()
