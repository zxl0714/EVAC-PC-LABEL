"""The real C++ worker owns round commits, HTTP steps, restart and cancellation."""
from contextlib import contextmanager
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'out/native/KetsuiNativeProbe.exe'
sys.path.insert(0, str(ROOT / 'server'))
import ketsui_live as live


@unittest.skipUnless(EXE.is_file(), 'Build-Native.ps1 required')
class RoundWorkerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='kt-round-worker-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.server_count = 0

    @contextmanager
    def service(self, handler=None):
        self.server_count += 1
        store = live.Store(self.root / f'service-{self.server_count}.sqlite3')
        server = live.Server(0, store)
        if handler:
            server.RequestHandlerClass = handler
        thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': 0.01})
        thread.start()
        config = self.root / f'community-{self.server_count}.ini'
        config.write_text('[community]\nenabled=true\nnickname=Ａlice\nendpoint=http://127.0.0.1:'
                          + str(server.server_port) + live.API + '\n', encoding='utf-8')
        try:
            yield store, config
        finally:
            server.shutdown()
            server.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive())

    def start(self, config, profile, phase):
        return subprocess.Popen([str(EXE), '--community-round-client-smoke', str(config), str(profile), phase],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding='utf-8', creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))

    def run_client(self, config, profile, phase, expected=0):
        process = self.start(config, profile, phase)
        try:
            stdout, stderr = process.communicate(timeout=20)
            self.assertEqual(process.returncode, expected, stdout + stderr)
            events = [json.loads(line) for line in stdout.splitlines()]
            if expected == 0:
                self.assertEqual(events[-1]['event'], 'community_round_client_smoke')
                self.assertTrue(events[-1]['passed'])
            return events, stdout + stderr
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=5)

    @staticmethod
    def pending(profile):
        return sorted((profile / 'transport/community-rounds').glob('r*/round.json'))

    def final_database(self, store, profile, before=None, nickname='Alice'):
        self.assertFalse(self.pending(profile))
        self.assertFalse(list((profile / 'transport/community-outbox').glob('q*/request.json')))
        self.assertFalse(list((profile / 'transport/community-rounds/.kt-transactions').iterdir()))
        receipt_path, = (profile / 'transport/community-rounds/.trash').rglob('round.json')
        receipt = json.loads(receipt_path.read_text())
        self.assertEqual(receipt_path.with_name('record.bin').stat().st_size, 0)
        self.assertEqual(receipt_path.with_name('attachment.zlib').stat().st_size, 0)
        with store.connect() as db:
            self.assertEqual(db.execute('pragma integrity_check').fetchone()[0], 'ok')
            scores = list(db.execute('select id,submission,board,score,nickname from scores order by board'))
            replays = list(db.execute('select entry_id,payload,sha256 from replays'))
        self.assertEqual([(r['board'], r['score'], r['nickname']) for r in scores], [(0, 9001, nickname), (1, 9001, nickname)])
        self.assertEqual([r['submission'] for r in scores], [b['submission_id'] for b in receipt['receipts']])
        self.assertEqual({r['entry_id'] for r in replays}, {r['id'] for r in scores})
        self.assertEqual(len(replays), 2)
        self.assertEqual(replays[0]['payload'], replays[1]['payload'])
        self.assertEqual(hashlib.sha256(replays[0]['payload']).hexdigest(), replays[0]['sha256'])
        raw = zlib.decompress(replays[0]['payload'])
        self.assertEqual(len(raw), 524288)
        if before:
            self.assertEqual(receipt['id'], before['id'])
            self.assertEqual([r['submission'] for r in scores], [b['submission_id'] for b in before['boards']])
            self.assertEqual(hashlib.sha256(raw).hexdigest(), before['record_sha256'])
            self.assertEqual(replays[0]['sha256'], before['attachment_sha256'])

    def test_online_round_uses_one_noncaller_worker_and_no_independent_replay_outbox(self):
        with self.service() as (store, config):
            profile = self.root / 'online'
            events, text = self.run_client(config, profile, 'create')
            self.assertTrue(events[-1]['durable'] and events[-1]['first_attempt_complete'])
            self.assertTrue(events[-1]['first_identity_verified'])
            staged = [e for e in events if e['event'] == 'native_community_round_staged']
            steps = [e for e in events if e['event'] == 'native_community_round_step']
            self.assertEqual(len(staged), 1)
            threads = {e['host_thread_id'] for e in staged + steps}
            self.assertEqual(len(threads), 1)
            self.assertNotIn(events[-1]['caller_thread_id'], threads)
            first_score = next(i for i, e in enumerate(events) if e['event'] == 'native_community_request' and e['operation'] == 'score')
            self.assertLess(events.index(staged[0]), first_score)
            self.assertNotIn('Alice', text)
            self.assertNotIn('Ａlice', text)
            self.final_database(store, profile)

    def test_original_native_envelope_is_proved_on_worker_without_reupload_or_receipt_rewrite(self):
        counts = {'scores': 0, 'replays': 0}
        class CountWrites(live.Handler):
            def do_POST(self):
                if self.path == live.API + '/scores':
                    counts['scores'] += 1
                if self.path == live.API + '/replays':
                    counts['replays'] += 1
                super().do_POST()
        with self.service(CountWrites) as (store, config):
            profile = self.root / 'native proof'
            events, _ = self.run_client(config, profile, 'proof')
            proofs = [e for e in events if e['event'] == 'native_community_round_replay_proof']
            self.assertEqual([e['error'] for e in proofs], [0, 5, 5])
            self.assertEqual(len({e['host_thread_id'] for e in proofs}), 1)
            self.assertNotEqual(proofs[0]['host_thread_id'], events[-1]['caller_thread_id'])
            self.assertEqual(counts, {'scores': 2, 'replays': 2})
            self.final_database(store, profile)

    def test_first_offline_stage_and_changed_current_nickname_recover_original_identity(self):
        online = threading.Event()
        identity_names = []
        class InitiallyOffline(live.Handler):
            def do_GET(self):
                if self.path == live.API + '/health' and not online.is_set():
                    self.reply(503, {'error': 'authored_offline'})
                    return
                super().do_GET()
            def read_json(self):
                value = super().read_json()
                if self.path == live.API + '/identity':
                    identity_names.append(value['nickname'])
                return value
        with self.service(InitiallyOffline) as (store, config):
            profile = self.root / 'first offline'
            events, _ = self.run_client(config, profile, 'offline')
            self.assertEqual(events[-1]['first_attempt_error'], 4)
            self.assertFalse(events[-1]['first_identity_verified'])
            self.assertTrue(all(s['score_status'] is None and s['replay_status'] is None for s in events[-1]['statuses']))
            path, = self.pending(profile)
            before = json.loads(path.read_text())
            self.assertIsNone(before['identity'])
            with store.connect() as db:
                self.assertEqual(db.execute('select count(*) from scores').fetchone()[0], 0)
            config.write_text(config.read_text().replace('Ａlice', 'New-Identity'))
            online.set()
            events, text = self.run_client(config, profile, 'drain')
            self.assertEqual(events[-1]['pending_rounds'], 0)
            self.assertIn('New-Identity', identity_names)
            self.assertIn('Ａlice', identity_names)
            self.assertNotIn('New-Identity', text)
            self.final_database(store, profile, before)

    def test_replay_failure_is_real_and_restart_automatically_finishes_the_other_board(self):
        fail = threading.Event()
        fail.set()
        class Replay503(live.Handler):
            def do_POST(self):
                if self.path == live.API + '/replays' and fail.is_set():
                    self.read_json()
                    self.reply(503, {'error': 'authored_replay_failure'})
                    return
                super().do_POST()
        with self.service(Replay503) as (store, config):
            profile = self.root / 'replay error'
            events, _ = self.run_client(config, profile, 'failure')
            self.assertEqual(events[-1]['first_attempt_error'], 4)
            self.assertEqual(events[-1]['statuses'], [{'board': 0, 'score_status': 200, 'replay_status': 503},
                                                     {'board': 1, 'score_status': None, 'replay_status': None}])
            path, = self.pending(profile)
            before = json.loads(path.read_text())
            self.assertIsNotNone(before['boards'][0]['score_receipt'])
            self.assertIsNone(before['boards'][1]['score_receipt'])
            self.assertFalse(list((profile / 'transport/community-outbox').glob('q*/request.json')))
            fail.clear()
            self.run_client(config, profile, 'drain')
            self.final_database(store, profile, before)

    def test_same_process_recovery_keeps_first_failure_immutable_and_finishes_background_delivery(self):
        failed, restore = threading.Event(), threading.Event()
        class RecoverReplay(live.Handler):
            def do_POST(self):
                if self.path == live.API + '/replays' and not restore.is_set():
                    self.read_json()
                    failed.set()
                    self.reply(503, {'error': 'authored_replay_failure'})
                    return
                super().do_POST()
        with self.service(RecoverReplay) as (store, config):
            profile = self.root / 'same process recovery'
            process = self.start(config, profile, 'recover')
            try:
                self.assertTrue(failed.wait(5))
                path, = self.pending(profile)
                before = json.loads(path.read_text())
                restore.set()
                stdout, stderr = process.communicate('!', timeout=18)
                self.assertEqual(process.returncode, 0, stdout + stderr)
                final = json.loads(stdout.splitlines()[-1])
                self.assertEqual(final['pending_rounds'], 0)
                self.assertEqual(final['first_attempt_error'], 4)
                self.assertFalse(final['first_attempt_complete'])
                self.final_database(store, profile, before)
            finally:
                restore.set()
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=5)

    def kill_after_commit(self, route):
        committed, release = threading.Event(), threading.Event()
        class HeldReply(live.Handler):
            def reply(self, status, value, *, binary_headers=None):
                if self.path == live.API + route and status == 200 and not committed.is_set():
                    committed.set()
                    release.wait(10)
                super().reply(status, value, binary_headers=binary_headers)
        with self.service(HeldReply) as (store, config):
            profile = self.root / 'killed'
            process = self.start(config, profile, 'create')
            try:
                self.assertTrue(committed.wait(5))
                self.assertIsNone(process.poll())
                process.kill()
                process.communicate(timeout=5)
                path, = self.pending(profile)
                before = json.loads(path.read_text())
                self.assertTrue(all(b['replay_receipt'] is None for b in before['boards']))
                self.assertIsNone(before['boards'][1]['score_receipt'])
                release.set()
                self.run_client(config, profile, 'drain')
                self.final_database(store, profile, before)
            finally:
                release.set()
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=5)

    def test_worker_restart_after_score_commit_before_ack_or_any_replay(self):
        self.kill_after_commit('/scores')

    def test_worker_restart_after_replay_commit_before_ack_or_second_board(self):
        self.kill_after_commit('/replays')

    def test_stop_cancels_active_round_http_and_preserves_the_durable_operation(self):
        entered, release, committed = threading.Event(), threading.Event(), threading.Event()
        class HeldScore(live.Handler):
            def read_json(self):
                value = super().read_json()
                if self.path == live.API + '/scores' and not entered.is_set():
                    entered.set()
                    release.wait(10)
                return value
            def reply(self, status, value, *, binary_headers=None):
                if self.path == live.API + '/scores' and status == 200:
                    committed.set()
                super().reply(status, value, binary_headers=binary_headers)
        with self.service(HeldScore) as (store, config):
            profile = self.root / 'cancelled'
            process = self.start(config, profile, 'cancel')
            try:
                self.assertTrue(entered.wait(5))
                stdout, stderr = process.communicate('!', timeout=5)
                self.assertEqual(process.returncode, 0, stdout + stderr)
                final = json.loads(stdout.splitlines()[-1])
                self.assertEqual(final['first_attempt_error'], 2)
                self.assertTrue(final['durable'])
                path, = self.pending(profile)
                before = json.loads(path.read_text())
                release.set()
                self.assertTrue(committed.wait(5))
                self.run_client(config, profile, 'drain')
                self.final_database(store, profile, before)
            finally:
                release.set()
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=5)

    def test_bounded_queued_rounds_are_cancelled_without_broken_futures_or_disk_commits(self):
        entered, release = threading.Event(), threading.Event()
        class HeldHealth(live.Handler):
            def do_GET(self):
                if self.path == live.API + '/health':
                    entered.set()
                    release.wait(10)
                super().do_GET()
        with self.service(HeldHealth) as (_, config):
            profile = self.root / 'queue cancelled'
            process = self.start(config, profile, 'queue-cancel')
            try:
                self.assertTrue(entered.wait(5))
                stdout, stderr = process.communicate('!', timeout=5)
                self.assertEqual(process.returncode, 0, stdout + stderr)
                final = json.loads(stdout.splitlines()[-1])
                self.assertEqual((final['cancelled'], final['queue_full']), (16, 2))
                self.assertLess(final['stop_ms'], 4500)
                self.assertFalse(self.pending(profile))
            finally:
                release.set()
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=5)

    def test_bad_owned_input_does_not_disable_the_worker_or_send_a_bad_score(self):
        with self.service() as (store, config):
            profile = self.root / 'invalid then valid'
            events, text = self.run_client(config, profile, 'invalid')
            self.assertEqual(sum(e['event'] == 'native_community_round_staged' for e in events), 1)
            self.assertNotIn('native_community_worker_unavailable', text)
            self.final_database(store, profile)

    def test_changed_endpoint_retains_round_without_sending_it_to_the_other_service(self):
        online = threading.Event()
        class Offline(live.Handler):
            def do_GET(self):
                if self.path == live.API + '/health' and not online.is_set():
                    self.reply(503, {'error': 'authored_offline'})
                    return
                super().do_GET()
        with self.service(Offline) as (original_store, original_config):
            profile = self.root / 'endpoint changed'
            self.run_client(original_config, profile, 'offline')
            path, = self.pending(profile)
            before_bytes = path.read_bytes()
            before = json.loads(before_bytes)
            with self.service() as (other_store, other_config):
                events, _ = self.run_client(other_config, profile, 'retained')
                self.assertEqual(events[-1]['pending_rounds'], 1)
                self.assertEqual(path.read_bytes(), before_bytes)
                with other_store.connect() as db:
                    self.assertEqual(db.execute('select count(*) from scores').fetchone()[0], 0)
            online.set()
            self.run_client(original_config, profile, 'drain')
            self.final_database(original_store, profile, before)

    def test_protocol_failure_preserves_round_and_never_claims_http_200_as_success(self):
        corrupt = threading.Event()
        corrupt.set()
        class InvalidReceipt(live.Handler):
            def reply(self, status, value, *, binary_headers=None):
                if self.path == live.API + '/scores' and status == 200 and corrupt.is_set():
                    value = dict(value, entry_id='invalid-entry')
                super().reply(status, value, binary_headers=binary_headers)
        with self.service(InvalidReceipt) as (store, config):
            profile = self.root / 'bad receipt'
            events, _ = self.run_client(config, profile, 'failure')
            self.assertEqual(events[-1]['first_attempt_error'], 5)
            self.assertFalse(events[-1]['first_attempt_complete'])
            self.assertEqual(events[-1]['statuses'][0]['score_status'], 200)
            path, = self.pending(profile)
            before = json.loads(path.read_text())
            self.assertIsNone(before['boards'][0]['score_receipt'])
            corrupt.clear()
            self.run_client(config, profile, 'drain')
            self.final_database(store, profile, before)

    def test_round_specific_receipt_shape_failure_does_not_poison_healthy_storage(self):
        corrupt = threading.Event()
        corrupt.set()
        class ExtraReceipt(live.Handler):
            def reply(self, status, value, *, binary_headers=None):
                if self.path == live.API + '/scores' and status == 200 and corrupt.is_set():
                    value = dict(value, unexpected='authored-extra')
                super().reply(status, value, binary_headers=binary_headers)
        with self.service(ExtraReceipt) as (store, config):
            profile = self.root / 'strict receipt shape'
            events, text = self.run_client(config, profile, 'failure')
            self.assertEqual(events[-1]['first_attempt_error'], 5)
            self.assertNotIn('native_community_worker_unavailable', text)
            path, = self.pending(profile)
            before = json.loads(path.read_text())
            self.assertIsNone(before['boards'][0]['score_receipt'])
            corrupt.clear()
            self.run_client(config, profile, 'drain')
            self.final_database(store, profile, before)

    def test_durable_round_capacity_rejects_without_dropping_earlier_offline_rounds(self):
        class Offline(live.Handler):
            def do_GET(self):
                self.reply(503, {'error': 'authored_offline'})
        with self.service(Offline) as (store, config):
            profile = self.root / 'capacity'
            events, _ = self.run_client(config, profile, 'capacity')
            self.assertEqual(events[-1]['pending_rounds'], 16)
            paths = self.pending(profile)
            self.assertEqual(len(paths), 16)
            references = set()
            for path in paths:
                metadata = json.loads(path.read_text())
                references.add(metadata['id'])
                references.update(b['submission_id'] for b in metadata['boards'])
                self.assertIsNone(metadata['identity'])
                self.assertEqual(path.with_name('record.bin').stat().st_size, 524288)
            self.assertEqual(len(references), 48)
            with store.connect() as db:
                self.assertEqual(db.execute('select count(*) from scores').fetchone()[0], 0)

    def test_corrupt_restored_storage_rejects_both_futures_without_sending_or_rewriting(self):
        class Offline(live.Handler):
            def do_GET(self):
                self.reply(503, {'error': 'authored_offline'})
        with self.service(Offline) as (store, config):
            profile = self.root / 'corrupt restored round'
            self.run_client(config, profile, 'offline')
            path, = self.pending(profile)
            payload = path.with_name('record.bin')
            raw = bytearray(payload.read_bytes())
            raw[-1] ^= 1
            payload.write_bytes(raw)
            original = {p.relative_to(path.parent): p.read_bytes() for p in path.parent.rglob('*') if p.is_file()}
            events, text = self.run_client(config, profile, 'storage')
            self.assertTrue(events[-1]['storage_error'])
            self.assertIn('native_community_worker_unavailable', text)
            self.assertEqual({p.relative_to(path.parent): p.read_bytes() for p in path.parent.rglob('*') if p.is_file()}, original)
            with store.connect() as db:
                self.assertEqual(db.execute('select count(*) from scores').fetchone()[0], 0)

    def test_not_best_receipts_finish_without_sending_or_fabricating_replay_results(self):
        with self.service() as (store, config):
            for board in (0, 1):
                store.submit(dict(submission_id=uuid.uuid4().hex, nickname='Alice', board=board, rules='standard', score='9002',
                                  comment_base64=base64.b64encode(bytes(64)).decode(), game_info_base64='', build='authored-seed'))
            profile = self.root / 'not best'
            events, _ = self.run_client(config, profile, 'not-best')
            self.assertTrue(events[-1]['first_attempt_complete'])
            self.assertTrue(all(s['score_status'] == 200 and s['replay_status'] is None for s in events[-1]['statuses']))
            with store.connect() as db:
                self.assertEqual(db.execute('select count(*) from scores').fetchone()[0], 4)
                self.assertEqual(db.execute('select count(*) from replays').fetchone()[0], 0)
            self.assertFalse(self.pending(profile))

    def test_disabled_client_creates_no_transport_or_round_directory(self):
        with self.service() as (_, config):
            profile = self.root / 'disabled'
            self.run_client(config, profile, 'disabled')
            self.assertFalse((profile / 'transport').exists())

    def test_existing_client_mode_requires_explicit_round_opt_in(self):
        with self.service() as (_, config):
            profile = self.root / 'mode off'
            self.run_client(config, profile, 'mode-off')
            self.assertFalse((profile / 'transport/community-rounds').exists())
