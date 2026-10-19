import base64
import copy
import hashlib
import http.client
import json
from pathlib import Path
import struct
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server.games.espgaluda2 import ApiError, Server, Store as G2Store
from server.games.sdoj import Store, SDOJ, VIEW_CHARACTER_TYPE, filetime, replay_info, REPLAY_FORMAT

TIMESTAMP = -5384438183559864396  # Authored field convention, low DWORD first.


def checksum(data):
    struct.pack_into('>H', data, 8, 0)
    total = sum(value * (index + 1) for index, (value,) in enumerate(struct.iter_unpack('>H', data))) & 0xffff
    struct.pack_into('>H', data, 8, total)
    return bytes(data)


def recording(score=12345, packed=48):
    # Deliberate nonzero padding verifies that native unused words are opaque,
    # not a G2 version header or fields to sanitize before transport.
    data = bytearray(b'\xbe' * 336)
    struct.pack_into('>I', data, 0, 0x01304250)
    struct.pack_into('>q', data, 24, score)
    struct.pack_into('>q', data, 32, TIMESTAMP)
    struct.pack_into('>III', data, 52, packed, 0, 0)
    struct.pack_into('>I', data, 76, 0)
    struct.pack_into('>I', data, 92, len(data))
    return checksum(data)


def submission(score=12345, view=23, name='Player', token='1' * 32):
    columns = [(0x20000004, 2, str(score)), (0x20000002, 2, str(TIMESTAMP)),
               (0x10000007, 1, VIEW_CHARACTER_TYPE[view]), (0x10000006, 1, 0),
               (0x10000005, 1, 0), (0x10000001, 1, 0), (0x10008011, 1, 0)]
    return dict(protocol=1, title_id=SDOJ.title, submission=token, nickname=name,
                view=view, rules='standard', score=str(score), build='authored-test',
                columns=[dict(id=key, type=typ, value=value) for key, typ, value in columns])


def attachment(score_id, data=None):
    data = recording() if data is None else data
    return dict(protocol=1, title_id=SDOJ.title, score_id=score_id, nickname='Player',
                format=REPLAY_FORMAT, data=base64.b64encode(data).decode(), sha256=hashlib.sha256(data).hexdigest())


class SdojCommunityServerTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.path = Path(self.folder.name) / 'sdoj.sqlite3'
        self.store = Store(self.path)

    def test_independent_native_view_ids_and_character_dress(self):
        self.assertEqual(len(SDOJ.views), 39)
        for view in SDOJ.views:
            self.store.submit(submission(view=view, token=f'{view:032x}'))
            self.assertEqual(self.store.leaderboard(view)['rows'][0]['score'], '12345')
        with self.assertRaises(ApiError):
            self.store.board(1, 'standard')  # Not inferred from G2's 1..84 range.
        bad = submission(); bad['columns'][2]['value'] = 32
        with self.assertRaisesRegex(ApiError, '400'):
            self.store.submit(bad)

    def test_signed_native_timestamp_and_lossless_64bit_score(self):
        score = 9007199254740993
        self.store.submit(submission(score))
        row = self.store.leaderboard(23)['rows'][0]
        self.assertEqual(row['score'], str(score))
        self.assertEqual(next(p['value'] for p in row['columns'] if p['id'] == 0x20000002), str(TIMESTAMP))
        self.assertEqual(filetime(TIMESTAMP), 0x01dd5fb4b546a200)

    def test_schema_does_not_accept_g2_score_or_cross_game_database(self):
        bad = submission(); bad['title_id'] = '435A07D8'
        with self.assertRaises(ApiError): self.store.submit(bad)
        bad = submission(); bad['columns'][0]['id'] = 0x20000001
        with self.assertRaises(ApiError): self.store.submit(bad)
        with self.assertRaises(RuntimeError): G2Store(self.path)

    def test_partial_unknown_duplicate_or_wrongly_typed_properties_rejected(self):
        variants = []
        body = submission(); body['columns'].pop(1); variants.append(body)
        body = submission(); body['columns'].append(dict(id=99, type=1, value=0)); variants.append(body)
        body = submission(); body['columns'].append(copy.deepcopy(body['columns'][0])); variants.append(body)
        body = submission(); body['columns'][1].update(type=1, value=1); variants.append(body)
        for body in variants:
            with self.subTest(body=body), self.assertRaises(ApiError): self.store.submit(body)

    def test_native_checksum_envelope_and_padding_are_preserved(self):
        data = recording()
        info = replay_info(data)
        self.assertEqual((info.score, info.character_type, info.stage), (12345, 48, 0))
        self.assertEqual(info.timestamp, TIMESTAMP)
        for offset in (0, 8, 24, 92, 320):
            bad = bytearray(data); bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ApiError): replay_info(bad)
        for bad in (data[:-1], data[:304], data + b'\0\0'):
            with self.assertRaises(ApiError): replay_info(bad)

    def test_replay_score_association_and_native_zero_storage_flag(self):
        sid = self.store.submit(submission())['id']
        self.assertIsNone(self.store.leaderboard(23)['rows'][0]['replay_id'])
        accepted = self.store.attach_replay(attachment(sid))
        row = self.store.leaderboard(23)['rows'][0]
        self.assertEqual(row['replay_format'], REPLAY_FORMAT)
        self.assertEqual(row['replay_size'], 336)
        self.assertEqual(base64.b64decode(self.store.replay(accepted['id'])['data']), recording())
        self.assertTrue(self.store.attach_replay(attachment(sid))['duplicate'])
        with self.assertRaises(ApiError): self.store.attach_replay(attachment(sid, recording(999)))
        with self.assertRaises(ApiError): self.store.attach_replay(attachment(sid, recording(packed=32)))

    def test_replay_mismatched_stage_time_option_or_size_rejected(self):
        sid = self.store.submit(submission())['id']
        for offset in (32, 56, 60, 76):
            data = bytearray(recording()); data[offset + 3] ^= 1; data = checksum(data)
            with self.subTest(offset=offset), self.assertRaises(ApiError): self.store.attach_replay(attachment(sid, data))
        body = submission(token='2' * 32); body['columns'][-1]['value'] = 338
        sid = self.store.submit(body)['id']
        with self.assertRaises(ApiError): self.store.attach_replay(attachment(sid))

    def test_personal_best_estimate_and_idempotent_submission(self):
        body = submission(); first = self.store.submit(body)
        self.assertEqual(self.store.submit(body)['id'], first['id'])
        self.store.submit(submission(100, name='Second', token='2' * 32))
        self.store.submit(submission(99, token='3' * 32))
        self.assertEqual([row['nickname'] for row in self.store.leaderboard(23)['rows']], ['Player', 'Second'])
        self.assertEqual(self.store.estimate(23, 'standard', '101', 'Player')['rank'], 1)

    def test_loopback_http_uses_own_route_and_identity(self):
        server = Server(('127.0.0.1', 0), self.store)
        worker = threading.Thread(target=server.serve_forever, daemon=True); worker.start()
        try:
            connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=3)
            connection.request('GET', '/cave/api/v1/games/sdoj/health')
            response = connection.getresponse(); body = json.loads(response.read())
            self.assertEqual(response.status, 200); self.assertEqual(body['title_id'], '435A07E7')
            connection.close()
            connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=3)
            connection.request('GET', '/cave/api/v1/games/espgaluda2/health')
            response = connection.getresponse(); response.read(); self.assertEqual(response.status, 404)
            connection.close()
        finally:
            server.shutdown(); server.server_close(); worker.join()


if __name__ == '__main__':
    unittest.main()
