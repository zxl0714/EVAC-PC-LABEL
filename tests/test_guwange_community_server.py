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
from server.games.guwange import Store, GUWANGE, REPLAY_FORMAT, replay_info
from server.backup import backup


def recording(mode=0, character=0, score=12345, raw_stage=0, second=False):
    # Independently authored Python fixture; deliberately opaque nonzero padding.
    data = bytearray([0x81] * (256 + 0x2cab8 + 35))
    magic = ('GWN_for_XB360_' + ('AC', 'BL', 'XB')[mode] + '20100820').encode() + b'\0'
    data[:len(magic)] = magic
    struct.pack_into('>IQIII', data, 36, len(data) - 256, 0x01dd63a61a501600, 2, mode, 0)
    struct.pack_into('>I', data, 60, raw_stage)
    struct.pack_into('>II', data, 76, 0xffffffff if second else character, 0 if second else score)
    struct.pack_into('>II', data, 104, character if second else 0xffffffff, score if second else 0)
    checksum = sum(int(v) - 256 if v & 128 else v for v in data[256:]) & 65535
    struct.pack_into('>H', data, 32, checksum)
    return bytes(data)


def submission(view=1, character=0, score=12345, stage=0, token='1' * 32, name='Player'):
    props = [(0x20000004, 2, str(score)), (0x10000008, 1, character), (0x10000006, 1, stage),
             (0x10000001, 1, 0), (0x10000003, 1, 0), (0x20000002, 2, str(0x01dd63a61b501600)),
             (0x10000005, 1, 0), (0x10008011, 1, 0)]
    return dict(protocol=1, title_id=GUWANGE.title, submission=token, nickname=name,
                view=view, rules='standard', score=str(score), build='authored-test',
                columns=[dict(id=key, type=kind, value=value) for key, kind, value in props])


def attachment(score_id, data):
    return dict(protocol=1, title_id=GUWANGE.title, score_id=score_id, nickname='Player',
                format=REPLAY_FORMAT, data=base64.b64encode(data).decode(), sha256=hashlib.sha256(data).hexdigest())


class GuwangeCommunityServerTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.path = Path(self.folder.name) / 'guwange.sqlite3'
        self.store = Store(self.path)

    def test_all_modes_and_character_boards_with_exact_replay(self):
        for view in GUWANGE.views:
            slot = (view - 1) % 4
            character = slot - 1 if slot else 2
            sid = self.store.submit(submission(view, character, token=f'{view:032x}'))['id']
            data = recording((view - 1) // 4, character)
            self.assertIsNone(self.store.leaderboard(view)['rows'][0]['replay_id'])
            attached = self.store.attach_replay(attachment(sid, data))
            row = self.store.leaderboard(view)['rows'][0]
            self.assertEqual(row['replay_size'], len(data))
            self.assertEqual(base64.b64decode(self.store.replay(attached['id'])['data']), data)
            self.assertTrue(self.store.attach_replay(attachment(sid, data))['duplicate'])

    def test_board_mode_is_bound_even_for_identical_score_and_character(self):
        for mode in range(3):
            view = mode * 4 + 1
            sid = self.store.submit(submission(view, token=f'{view:032x}'))['id']
            for other in range(3):
                if other != mode:
                    with self.assertRaises(ApiError): self.store.attach_replay(attachment(sid, recording(other)))

    def test_native_stage_player_and_separate_time(self):
        sid = self.store.submit(submission(stage=4))['id']
        data = recording(raw_stage=5, second=True)
        info = replay_info(data)
        self.assertEqual(info.stage, 4)
        self.assertNotEqual(info.filetime, int(submission()['columns'][5]['value']))
        self.store.attach_replay(attachment(sid, data))
        for other in (recording(raw_stage=4, score=999), recording(raw_stage=6), recording(character=1)):
            with self.assertRaises(ApiError): self.store.attach_replay(attachment(sid, other))

    def test_bad_envelopes_checksum_and_nonzero_padding(self):
        data = recording()
        self.assertEqual(replay_info(data).players[0], (0, 12345))
        for offset in (0, 24, 32, 36, 40, 48, 52, 56, 76, 104, 256):
            bad = bytearray(data); bad[offset] ^= 0x80
            with self.subTest(offset=offset), self.assertRaises(ApiError): replay_info(bad)
        for bad in (b'', data[:255], data[:-1], data + b'\0'):
            with self.assertRaises(ApiError): replay_info(bad)

    def test_partial_wrong_view_type_and_cross_game_rejected(self):
        bodies = []
        bad = submission(); bad['view'] = 13; bodies.append(bad)
        bad = submission(2, character=1); bodies.append(bad)
        bad = submission(); bad['columns'].pop(3); bodies.append(bad)
        bad = submission(); bad['columns'].append(copy.deepcopy(bad['columns'][0])); bodies.append(bad)
        bad = submission(); bad['columns'][0]['type'] = 1; bodies.append(bad)
        bad = submission(); bad['columns'][3]['value'] = 1; bodies.append(bad)
        bad = submission(); bad['columns'][-1]['value'] = 1024; bodies.append(bad)
        bad = submission(); bad['title_id'] = '435A07D8'; bodies.append(bad)
        bodies.append(submission(score=0x80000000))
        for bad in bodies:
            with self.subTest(bad=bad), self.assertRaises(ApiError): self.store.submit(bad)
        with self.assertRaises(RuntimeError): G2Store(self.path)

    def test_size_and_character_mismatch(self):
        body = submission(); body['columns'][-1]['value'] = len(recording()) + 1
        sid = self.store.submit(body)['id']
        with self.assertRaises(ApiError): self.store.attach_replay(attachment(sid, recording()))

    def test_best_rank_and_http_identity(self):
        body = submission(); first = self.store.submit(body)
        self.assertEqual(self.store.submit(body)['id'], first['id'])
        self.store.submit(submission(score=100, name='Rival', token='2' * 32))
        self.assertEqual(self.store.estimate(1, 'standard', '101', 'Player')['rank'], 1)
        server = Server(('127.0.0.1', 0), self.store)
        worker = threading.Thread(target=server.serve_forever, daemon=True); worker.start()
        try:
            for game, status in (('guwange', 200), ('sdoj', 404), ('espgaluda2', 404)):
                connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=3)
                connection.request('GET', f'/cave/api/v1/games/{game}/health')
                response = connection.getresponse(); body = json.loads(response.read())
                self.assertEqual(response.status, status)
                if status == 200: self.assertEqual(body['title_id'], GUWANGE.title)
                connection.close()
        finally:
            server.shutdown(); server.server_close(); worker.join()

    def test_backup_restores_title_scores_and_exact_replays(self):
        expected = {}
        for mode in range(3):
            view = mode * 4 + 1
            score_id = self.store.submit(submission(view, token=f'{view:032x}'))['id']
            data = recording(mode)
            replay_id = self.store.attach_replay(attachment(score_id, data))['id']
            expected[view] = (self.store.leaderboard(view), replay_id, data)

        snapshot = backup(self.path, self.path.parent / 'backups')
        restored = Store(snapshot)
        with restored.connect() as db:
            self.assertEqual(db.execute('PRAGMA integrity_check').fetchall(), [('ok',)])
            self.assertEqual(db.execute('SELECT * FROM identity').fetchall(), [('guwange', '58410A78', 1)])
            self.assertEqual(db.execute('PRAGMA foreign_key_check').fetchall(), [])
        for view, (board, replay_id, data) in expected.items():
            self.assertEqual(restored.leaderboard(view), board)
            self.assertEqual(self.store.leaderboard(view), board)
            result = restored.replay(replay_id)
            self.assertEqual(base64.b64decode(result['data']), data)
            self.assertEqual(result['sha256'], hashlib.sha256(data).hexdigest())

        # Continued writes to the live database cannot alter the saved snapshot.
        self.store.submit(submission(score=99999, token='f' * 32))
        self.assertEqual(self.store.leaderboard(1)['rows'][0]['score'], '99999')
        self.assertEqual(restored.leaderboard(1), expected[1][0])
        with self.assertRaises(RuntimeError):
            G2Store(snapshot)


if __name__ == '__main__':
    unittest.main()
