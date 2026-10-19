"""Owned round-journal interruption tests; no game files or playable snapshots."""
import hashlib
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import unittest
import zlib
from contextlib import contextmanager

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'out/native/KetsuiNativeProbe.exe'
sys.path.insert(0, str(ROOT / 'server'))
import ketsui_live as live


@unittest.skipUnless(os.name == 'nt' and EXE.is_file(), 'Build-Native.ps1 required')
class RoundJournalTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='kt-round-journal-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def invoke(self, directory, phase):
        result = subprocess.run([str(EXE), '--community-round-journal-fixture', str(directory), phase],
                                capture_output=True, text=True, encoding='utf-8', timeout=15,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        event, = [e for e in events if e['event'] == 'community_round_fixture_snapshot']
        return event['rounds']

    @contextmanager
    def service(self, handler=None):
        store = live.Store(self.root / 'service.sqlite3')
        server = live.Server(0, store)
        if handler:
            server.RequestHandlerClass = handler
        thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': 0.01})
        thread.start()
        config = self.root / 'community.ini'
        config.write_text('[community]\nenabled=true\nnickname=Ｒound-Fixture\nendpoint=http://127.0.0.1:'
                          + str(server.server_port) + live.API + '\n', encoding='utf-8')
        try:
            yield store, config
        finally:
            server.shutdown()
            server.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive())

    def smoke(self, config, directory, phase, expected=0):
        result = subprocess.run([str(EXE), '--community-round-journal-smoke', str(config), str(directory), phase],
                                capture_output=True, text=True, encoding='utf-8', timeout=20,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        if expected == 0:
            self.assertTrue(json.loads(result.stdout.splitlines()[-1])['passed'])
        return result

    @contextmanager
    def parked(self, directory, phase):
        child = subprocess.Popen([str(EXE), '--community-round-journal-fixture', str(directory), phase],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8',
                                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        lines = queue.Queue()
        def pump():
            try:
                for line in child.stdout:
                    lines.put(line)
            finally:
                lines.put(None)
        reader = threading.Thread(target=pump, daemon=True)
        reader.start()
        try:
            while True:
                line = lines.get(timeout=15)
                self.assertIsNotNone(line, 'Writer exited before the exact checkpoint')
                event = json.loads(line)
                if event.get('event') == 'community_round_fixture_checkpoint':
                    self.assertEqual(event['point'], phase)
                    self.assertIsNone(child.poll())
                    break
            yield child
        finally:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)
            reader.join(timeout=5)
            self.assertFalse(reader.is_alive())
            child.stdout.close()
            child.stderr.close()

    @staticmethod
    def committed(directory):
        return sorted((directory / 'journal').glob('r*/round.json'))

    def validate_bodies(self, directory, snapshots):
        files = self.committed(directory)
        self.assertEqual(len(files), len(snapshots))
        by_id = {r['metadata']['id']: r for r in snapshots}
        for path in files:
            metadata = json.loads(path.read_text())
            self.assertEqual(metadata, by_id[metadata['id']]['metadata'])
            raw = path.with_name('record.bin').read_bytes()
            attachment = path.with_name('attachment.zlib').read_bytes()
            self.assertEqual(len(raw), 524288)
            decoder = zlib.decompressobj()
            decoded = decoder.decompress(attachment, 524289)
            self.assertEqual(decoded, raw)
            self.assertTrue(decoder.eof)
            self.assertFalse(decoder.unused_data or decoder.unconsumed_tail)
            self.assertEqual(hashlib.sha256(raw).hexdigest(), metadata['record_sha256'])
            self.assertEqual(hashlib.sha256(attachment).hexdigest(), metadata['attachment_sha256'])
        self.assertFalse(list((directory / 'journal/.kt-transactions').iterdir()))

    def test_owned_inputs_receipts_capacity_and_uncertain_write_fail_closed(self):
        result = subprocess.run([str(EXE), '--community-round-journal-self-test', str(self.root / 'self test')],
                                capture_output=True, text=True, encoding='utf-8', timeout=30,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(events[-1]['event'], 'community_round_journal_self_test')
        self.assertTrue(events[-1]['passed'])
        self.assertGreaterEqual(events[-1]['cases'], 35)
        self.assertTrue(all(e.get('passed', True) for e in events))
        self.assertFalse(any(e['event'].startswith('native_community_request') for e in events))

    def test_reopen_before_any_http_preserves_every_board_reference_and_full_record(self):
        directory = self.root / 'offline round'
        first = self.invoke(directory, 'seed')
        second = self.invoke(directory, 'inspect')
        self.assertEqual(second, first)
        self.assertEqual([b['board'] for b in first[0]['metadata']['boards']], [0, 1])
        self.assertTrue(all(b['score_receipt'] is None and b['replay_receipt'] is None
                            for b in first[0]['metadata']['boards']))
        self.validate_bodies(directory, second)

    def test_live_writer_excludes_a_second_process_before_it_can_recover_or_read_stale_state(self):
        directory = self.root / 'live writer'
        before = self.invoke(directory, 'seed')
        with self.parked(directory, 'score_file_flush'):
            result = subprocess.run([str(EXE), '--community-round-journal-fixture', str(directory), 'inspect'],
                                    capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn(b'native_save_transaction_recovered', result.stdout)
        after = self.invoke(directory, 'inspect')
        self.assertEqual(after, before)
        self.validate_bodies(directory, after)

    def test_score_ack_kill_recovers_whole_old_or_whole_new_generation(self):
        for phase in ('file_flush', 'ready', 'previous_moved', 'published'):
            with self.subTest(phase=phase):
                directory = self.root / phase
                first, = self.invoke(directory, 'seed')
                with self.parked(directory, 'score_' + phase):
                    pass
                after, = self.invoke(directory, 'inspect')
                self.assertEqual(after['metadata']['id'], first['metadata']['id'])
                self.assertEqual([b['submission_id'] for b in after['metadata']['boards']],
                                 [b['submission_id'] for b in first['metadata']['boards']])
                self.assertEqual((after['raw_sha256'], after['attachment_sha256']),
                                 (first['raw_sha256'], first['attachment_sha256']))
                boards = after['metadata']['boards']
                self.assertEqual(boards[0]['score_receipt'] is not None, phase != 'file_flush')
                self.assertIsNone(boards[0]['replay_receipt'])
                self.assertIsNone(boards[1]['score_receipt'])
                self.assertFalse(after['complete'])
                self.validate_bodies(directory, [after])

    def test_new_round_kill_recovers_all_boards_and_bodies_or_no_new_round(self):
        for phase in ('file_flush', 'ready', 'published'):
            with self.subTest(phase=phase):
                directory = self.root / phase
                baseline, = self.invoke(directory, 'seed')
                with self.parked(directory, 'new_' + phase):
                    captured = [json.loads(p.read_text()) for p in (directory / 'journal').rglob('round.json')]
                    new, = [m for m in captured if m['id'] != baseline['metadata']['id']]
                after = self.invoke(directory, 'inspect')
                self.assertEqual(len(after), 1 if phase == 'file_flush' else 2)
                self.assertIn(baseline, after)
                if phase != 'file_flush':
                    restored, = [r for r in after if r['metadata']['id'] == new['id']]
                    self.assertEqual(restored['metadata'], new)
                    self.assertIsNone(new['identity'])
                self.validate_bodies(directory, after)

    def test_archive_kill_keeps_completed_round_or_retires_small_receipts_only(self):
        for phase in ('file_flush', 'ready', 'previous_moved', 'published'):
            with self.subTest(phase=phase):
                directory = self.root / phase
                self.invoke(directory, 'seed')
                with self.parked(directory, 'archive_' + phase):
                    pass
                after = self.invoke(directory, 'inspect')
                self.assertEqual(len(after), 1 if phase == 'file_flush' else 0)
                if after:
                    self.assertTrue(after[0]['complete'])
                self.validate_bodies(directory, after)
                if phase != 'file_flush':
                    receipts = list((directory / 'journal/.trash').rglob('round.json'))
                    self.assertEqual(len(receipts), 1)
                    receipt = json.loads(receipts[0].read_text())
                    self.assertEqual(receipt['state'], 'delivered')
                    self.assertEqual(receipts[0].with_name('record.bin').stat().st_size, 0)
                    self.assertEqual(receipts[0].with_name('attachment.zlib').stat().st_size, 0)

    def test_corrupt_body_and_reference_are_rejected_without_rewriting_evidence(self):
        for field in ('record', 'reference'):
            with self.subTest(field=field):
                directory = self.root / field
                self.invoke(directory, 'seed')
                path, = self.committed(directory)
                if field == 'record':
                    body = path.with_name('record.bin')
                    raw = bytearray(body.read_bytes())
                    raw[-1] ^= 1
                    body.write_bytes(raw)
                else:
                    metadata = json.loads(path.read_text())
                    metadata['boards'][1]['submission_id'] = metadata['boards'][0]['submission_id']
                    path.write_text(json.dumps(metadata))
                before = {p.relative_to(directory): p.read_bytes() for p in directory.rglob('*') if p.is_file()}
                result = subprocess.run([str(EXE), '--community-round-journal-fixture', str(directory), 'inspect'],
                                        capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 2)
                self.assertEqual({p.relative_to(directory): p.read_bytes() for p in directory.rglob('*') if p.is_file()}, before)

    def http_commit_kill_recover(self, path):
        committed, release = threading.Event(), threading.Event()
        received = []
        class LostAcknowledgement(live.Handler):
            def read_json(self):
                value = super().read_json()
                if self.path in (live.API + '/scores', live.API + '/replays'):
                    received.append((self.path, value))
                return value
            def reply(self, status, value, *, binary_headers=None):
                if self.path == live.API + path and status == 200 and not committed.is_set():
                    committed.set()
                    release.wait(10)
                super().reply(status, value, binary_headers=binary_headers)
        with self.service(LostAcknowledgement) as (store, config):
            directory = self.root / 'actual http round'
            child = subprocess.Popen([str(EXE), '--community-round-journal-smoke', str(config), str(directory), 'create'],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8',
                                     creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            try:
                self.assertTrue(committed.wait(5), 'No committed request reached the reply barrier')
                self.assertIsNone(child.poll())
                child.kill()
                child.communicate(timeout=5)
                metadata_path, = self.committed(directory)
                before = json.loads(metadata_path.read_text())
                raw = metadata_path.with_name('record.bin').read_bytes()
                attachment = metadata_path.with_name('attachment.zlib').read_bytes()
                self.assertEqual(zlib.decompress(attachment), raw)
                self.assertEqual(len(before['boards']), 2)
                self.assertIsNone(before['boards'][1]['score_receipt'])
                self.assertTrue(all(b['replay_receipt'] is None for b in before['boards']))
                if path == '/scores':
                    self.assertIsNone(before['boards'][0]['score_receipt'])
                else:
                    self.assertIsNotNone(before['boards'][0]['score_receipt'])
                release.set()
                self.smoke(config, directory, 'drain')
                self.assertFalse(self.committed(directory))
                self.assertFalse(list((directory / 'journal/.kt-transactions').iterdir()))
                receipt_path, = (directory / 'journal/.trash').rglob('round.json')
                receipt = json.loads(receipt_path.read_text())
                self.assertEqual(receipt['id'], before['id'])
                self.assertEqual([b['submission_id'] for b in receipt['receipts']],
                                 [b['submission_id'] for b in before['boards']])
                with store.connect() as db:
                    self.assertEqual(db.execute('pragma integrity_check').fetchone()[0], 'ok')
                    scores = list(db.execute('select id,submission,board,score,nickname from scores order by board'))
                    replays = list(db.execute('select entry_id,payload,sha256 from replays'))
                self.assertEqual([(r['board'], r['score'], r['nickname']) for r in scores],
                                 [(0, 9001, 'Round-Fixture'), (1, 9001, 'Round-Fixture')])
                self.assertEqual([r['submission'] for r in scores], [b['submission_id'] for b in before['boards']])
                self.assertEqual({r['entry_id'] for r in replays}, {r['id'] for r in scores})
                self.assertEqual(len(replays), 2)
                self.assertTrue(all(r['payload'] == attachment and r['sha256'] == before['attachment_sha256'] for r in replays))
                for target, body in received:
                    board = next(r['board'] for r in scores if r['submission'] == body['submission_id'])
                    self.assertEqual(body['submission_id'], before['boards'][board]['submission_id'])
                    if target.endswith('/replays'):
                        self.assertEqual(live.binary(body['payload_base64'], live.MAX_REPLAY), attachment)
                self.assertFalse(list((directory / 'transport/community-outbox').glob('q*/request.json')))
            finally:
                release.set()
                if child.poll() is None:
                    child.kill()
                    child.communicate(timeout=5)

    def test_first_score_committed_but_neither_replay_called_recovers_entire_round(self):
        self.http_commit_kill_recover('/scores')

    def test_first_replay_committed_but_no_ack_or_second_board_recovers_same_bytes(self):
        self.http_commit_kill_recover('/replays')

    def test_http_fixture_refuses_public_or_changed_identity_before_transport(self):
        directory = self.root / 'public refused'
        config = self.root / 'public.ini'
        config.write_text('[community]\nenabled=true\nnickname=Round-Fixture\nendpoint=https://example.invalid/ketsui/api/v1\n')
        self.smoke(config, directory, 'create', expected=2)
        self.assertFalse(directory.exists())
        with self.service() as (store, config):
            directory = self.root / 'staged offline'
            self.smoke(config, directory, 'stage')
            path, = self.committed(directory)
            metadata = json.loads(path.read_text())
            self.assertIsNone(metadata['identity'])
            before = {p.relative_to(directory): p.read_bytes() for p in directory.rglob('*') if p.is_file()}
            original = config.read_text()
            config.write_text(original.replace('Ｒound-Fixture', 'Another-Fixture'))
            result = self.smoke(config, directory, 'drain', expected=2)
            self.assertNotIn('native_community_request', result.stdout)
            self.assertFalse((directory / 'transport').exists())
            self.assertEqual({p.relative_to(directory): p.read_bytes() for p in directory.rglob('*') if p.is_file()}, before)
            config.write_text(original)
            self.smoke(config, directory, 'drain')
            with store.connect() as db:
                self.assertEqual(db.execute('select count(*) from scores').fetchone()[0], 2)
                self.assertEqual(db.execute('select count(*) from replays').fetchone()[0], 2)


if __name__ == '__main__':
    unittest.main()
