import base64
from contextlib import closing
import hashlib
import http.client
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import threading
import unittest
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server.games import daioujou, ketsui


def score(board, value='123', name='Alice'):
    return dict(submission_id=uuid.uuid4().hex, nickname=name, board=board, rules='standard',
                score=value, comment_base64=base64.b64encode(bytes(64)).decode(),
                game_info_base64='', build='authored-profile-test')


class Ps4CommunityProfiles(unittest.TestCase):
    def test_native_mapping_units_and_identity_are_not_shared(self):
        self.assertEqual(ketsui.account_id('Alice'), '392319542754098719')
        self.assertEqual(daioujou.account_id('Alice'), '4491676318865695736')
        self.assertEqual(len(daioujou.BOARDS), 49)
        self.assertEqual(daioujou.BOARDS[6]['ship'], 'all')
        self.assertEqual(daioujou.BOARDS[0]['doll'], 'shotia')
        self.assertEqual(daioujou.BOARDS[1]['ship'], 'type_b')
        self.assertEqual(daioujou.BOARDS[4]['doll'], 'exy')
        self.assertEqual(daioujou.BOARDS[66]['mode'], 'black_label_one_loop')

    def test_scores_personal_readback_limits_and_custom_exclusion(self):
        with tempfile.TemporaryDirectory() as temporary:
            store = daioujou.Store(Path(temporary) / 'daioujou.sqlite3')
            request = score(6, '123456789')
            receipt = store.submit(request)
            self.assertTrue(receipt['accepted'])
            self.assertEqual(receipt['account_id'], daioujou.account_id('Alice'))
            self.assertEqual(store.submit(request), receipt)
            page = store.leaderboard(6, 'standard', account=receipt['account_id'])
            self.assertEqual(page['entries'][0]['score'], '123456789')
            for request in (score(10), score(16), score(99), score(6, '1000000000')):
                with self.assertRaises(ketsui.ApiError):
                    store.submit(request)
            self.assertEqual(store.leaderboard(6, 'standard')['total'], 1)
            self.assertEqual(store.leaderboard(66, 'standard')['total'], 0)

    def test_peer_database_is_refused_before_mutation(self):
        with tempfile.TemporaryDirectory() as temporary:
            for original, peer in ((ketsui.Store, daioujou.Store), (daioujou.Store, ketsui.Store)):
                path = Path(temporary) / (original.title + '.sqlite3')
                store = original(path)
                store.submit(score(0))
                before = hashlib.sha256(path.read_bytes()).hexdigest()
                with self.assertRaises(RuntimeError):
                    peer(path)
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)
                with closing(sqlite3.connect(path)) as db:
                    self.assertEqual(db.execute('SELECT title FROM ketsui_meta').fetchone(), (original.title,))

    def test_simultaneous_http_services_do_not_replace_global_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            servers = [ketsui.Server(0, cls(Path(temporary) / (cls.title + '.sqlite3')))
                       for cls in (ketsui.Store, daioujou.Store)]
            threads = [threading.Thread(target=server.serve_forever, kwargs=dict(poll_interval=0.01))
                       for server in servers]
            for thread in threads:
                thread.start()
            try:
                for server in servers:
                    profile = server.store
                    connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=5)
                    connection.request('GET', profile.api + '/health')
                    response = connection.getresponse()
                    body = json.loads(response.read())
                    self.assertEqual(response.status, 200)
                    self.assertEqual(body['title_id'], profile.title)
                    self.assertEqual(body['game_version'], profile.version)
                    self.assertEqual(body['features'], list(profile.features))
                    connection.close()
                    connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=5)
                    other = daioujou.Store.api if profile.title == ketsui.TITLE else ketsui.API
                    connection.request('GET', other + '/health')
                    response = connection.getresponse()
                    response.read()
                    self.assertEqual(response.status, 404)
                    connection.close()
            finally:
                for server in servers:
                    server.shutdown()
                    server.server_close()
                for thread in threads:
                    thread.join(5)
                    self.assertFalse(thread.is_alive())

    def test_invalid_replay_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            store = daioujou.Store(Path(temporary) / 'daioujou.sqlite3')
            request = score(6)
            receipt = store.submit(request)
            payload = b'AUTHORED-NOT-A-NATIVE-REPLAY'
            upload = dict(entry_id=receipt['entry_id'], submission_id=request['submission_id'], nickname='Alice',
                          format=store.replay_format, sha256=hashlib.sha256(payload).hexdigest(),
                          payload_base64=base64.b64encode(payload).decode())
            self.assertIn('replays', store.features)
            with self.assertRaises(ketsui.ApiError) as error:
                store.upload(upload)
            self.assertEqual(error.exception.status, 400)
            self.assertIsNone(store.leaderboard(6, 'standard')['entries'][0]['replay'])

    def test_authored_native_envelope_matches_score_and_selected_board(self):
        import zlib
        sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/ps4'))
        from replay_fixture import record
        payload=zlib.compress(record('daioujou'))
        info=daioujou.replay_info(payload)
        self.assertEqual((info['score'],info['character'],info['data_version']),(12345,0,0x10000))
        with tempfile.TemporaryDirectory() as temporary:
            store=daioujou.Store(Path(temporary)/'fixture.sqlite3')
            request=score(6,'12345')
            receipt=store.submit(request)
            upload=dict(entry_id=receipt['entry_id'],submission_id=request['submission_id'],nickname='Alice',
                format=store.replay_format,sha256=hashlib.sha256(payload).hexdigest(),payload_base64=base64.b64encode(payload).decode())
            self.assertEqual(store.upload(upload)['size'],len(payload))
            self.assertEqual(store.download(receipt['entry_id'])[0],payload)
            wrong=score(1,'12345')
            receipt=store.submit(wrong)
            upload.update(entry_id=receipt['entry_id'],submission_id=wrong['submission_id'])
            with self.assertRaises(ketsui.ApiError) as error:
                store.upload(upload)
            self.assertEqual(error.exception.code,'native_replay_mismatch')


if __name__ == '__main__':
    unittest.main()
