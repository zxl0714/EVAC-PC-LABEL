"""Real Windows C++ transport and crash/retry tests using an authored local service."""
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'out/ps4-native/KetsuiNativeProbe.exe'
sys.path.insert(0, str(ROOT))
from server.games import ketsui as live


@unittest.skipUnless(EXE.exists(), 'Build-Native.ps1 required')
class ClientTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='kt-community-client-')
        self.root = Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)

    def run_native(self, *args, expected=0, timeout=20):
        result = subprocess.run([str(EXE), *map(str, args)], capture_output=True, text=True,
                                encoding='utf-8', timeout=timeout,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertTrue(all(isinstance(event, dict) for event in events))
        return result, events

    @contextmanager
    def service(self, handler=None):
        store = live.Store(self.root / 'service.sqlite3')
        server = live.Server(0, store)
        if handler:
            server.RequestHandlerClass = handler
        thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': 0.01})
        thread.start()
        config = self.root / 'community.ini'
        config.write_text('[community]\nenabled=true\nnickname=Ａlice\nendpoint=http://127.0.0.1:'
                          + str(server.server_port) + live.API + '\n', encoding='utf-8')
        try:
            yield store, config
        finally:
            server.shutdown()
            server.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive())

    def test_config_protocol_crypto_disabled_client_and_transaction_outbox(self):
        _, events = self.run_native('--community-client-self-test', self.root / 'fixtures')
        cases = [event for event in events if event['event'] == 'community_client_self_test_case']
        self.assertGreaterEqual(len(cases), 30)
        self.assertTrue(all(event['passed'] for event in cases))
        self.assertTrue(events[-1]['passed'])

    def test_round_audit_reads_owned_bounded_snapshots_without_claiming_persistence(self):
        _, events = self.run_native('--community-round-audit-self-test')
        self.assertEqual(events[-1]['event'], 'community_round_audit_self_test')
        self.assertTrue(events[-1]['passed'])
        self.assertGreaterEqual(events[-1]['cases'], 19)
        self.assertTrue(all(e.get('passed', True) for e in events))
        observed = [e for e in events if e['event'] == 'native_community_round_audit']
        self.assertEqual(len(observed), 1)
        self.assertTrue(observed[0]['valid'])
        self.assertTrue(observed[0]['observational_only'])
        self.assertFalse(observed[0]['envelope_validated'])
        self.assertEqual((observed[0]['boards'], observed[0]['score']), (['0', '1'], 3850))

    def test_native_round_factory_job_scope_and_lifecycle_use_owned_per_batch_objects(self):
        directory = self.root / 'native-round-authored'
        _, events = self.run_native('--native-round-self-test', directory)
        self.assertEqual(events[-1]['event'], 'native_round_self_test')
        self.assertTrue(events[-1]['passed'])
        self.assertGreaterEqual(events[-1]['cases'], 35)
        self.assertTrue(all(e.get('passed', True) for e in events))
        self.assertFalse(directory.exists())
        self.assertFalse(any(e['event'] == 'native_community_request' for e in events))

    def test_standalone_config_check_redacts_nickname_and_does_not_touch_file(self):
        config = self.root / 'community.ini'
        content = '[community]\nenabled=false\nnickname=Private fixture name\n'
        config.write_text(content, encoding='utf-8')
        before = hashlib.sha256(config.read_bytes()).hexdigest()
        result, events = self.run_native('--community-config-check', config)
        self.assertNotIn('Private fixture name', result.stdout + result.stderr)
        self.assertEqual(events[-1], {'event': 'community_config_check', 'enabled': False, 'valid': True})
        self.assertEqual(hashlib.sha256(config.read_bytes()).hexdigest(), before)

    def test_root_config_template_checks_network_without_connecting_or_creating_storage(self):
        config = self.root / 'config.ini'
        content = (ROOT / 'config/input.ini').read_bytes()
        config.write_bytes(content)
        result, events = self.run_native('--community-config-check', config)
        self.assertEqual(events, [{'event': 'community_config_check', 'enabled': True, 'valid': True}])
        self.assertNotIn('Colonel-404', result.stdout + result.stderr)
        self.assertEqual(config.read_bytes(), content)
        self.assertEqual(set(self.root.iterdir()), {config})

    def test_cpp_winhttp_identity_scores_rankings_and_exact_replay_bytes(self):
        with self.service() as (store, config):
            profile = self.root / 'fresh-client'
            result, events = self.run_native('--community-client-smoke', config, profile)
            self.assertEqual(events[-1]['event'], 'community_client_smoke')
            self.assertTrue(events[-1]['passed'])
            self.assertNotIn('Alice', result.stdout + result.stderr)
            page = store.leaderboard(0, 'standard')
            self.assertEqual(page['total'], 1)
            self.assertEqual(page['entries'][0]['nickname'], 'Alice')
            self.assertEqual(page['entries'][0]['account_id'], live.account_id('Alice'))
            self.assertEqual(page['entries'][0]['score'], '9007199254740993')
            self.assertIsNotNone(page['entries'][0]['replay'])
            with store.connect() as db:
                self.assertEqual(db.execute('SELECT COUNT(*) FROM scores').fetchone()[0], 2)
            queue = profile / 'community-outbox'
            self.assertFalse(any(path.is_dir() and not path.name.startswith('.') for path in queue.iterdir()))
            receipts = list((queue / '.trash').rglob('request.json'))
            self.assertEqual(len(receipts), 3)
            for receipt in receipts:
                self.assertLess(receipt.stat().st_size, 4096)
                record = json.loads(receipt.read_text(encoding='utf-8'))
                self.assertEqual(record['state'], 'delivered')
                self.assertNotIn('body', record)

    def test_worker_validates_replay_envelopes_and_pins_original_entry_after_new_best(self):
        from test_community_replay import record
        counts = {'scores': 0, 'replays': 0}
        class CountWrites(live.Handler):
            def do_POST(self):
                if self.path == live.API + '/scores':
                    counts['scores'] += 1
                if self.path == live.API + '/replays':
                    counts['replays'] += 1
                super().do_POST()
        payload = zlib.compress(record())
        path = self.root / '自造 不可播放.attachment'
        path.write_bytes(payload)
        with self.service(CountWrites) as (store, config):
            profile = self.root / 'typed-replay-client'
            result, events = self.run_native('--community-replay-envelope-smoke', config, profile, path)
            self.assertEqual(events[-1]['event'], 'community_replay_envelope_smoke')
            self.assertTrue(events[-1]['passed'])
            self.assertFalse(events[-1]['native_playback_validated'])
            validations = [event for event in events if event['event'] == 'native_community_replay_validation']
            self.assertEqual([(e['operation'], e['passed']) for e in validations],
                             [('upload', False), ('upload', False), ('upload', True), ('download', False),
                              ('download', True), ('download', False), ('download', False)])
            worker_ids = {event['host_thread_id'] for event in validations}
            self.assertEqual(len(worker_ids), 1)
            self.assertNotIn(events[-1]['caller_thread_id'], worker_ids)
            self.assertEqual(counts, {'scores': 2, 'replays': 2}, 'Rejected upload must never reach the service')
            self.assertEqual(store.leaderboard(0, 'standard')['entries'][0]['score'], '9002')
            self.assertEqual(len(list((profile / 'community-outbox/.trash').rglob('request.json'))), 4)
            self.assertFalse(list((profile / 'community-outbox').glob('q*/request.json')))
            self.assertNotIn('Alice', result.stdout + result.stderr)
            self.assertEqual(path.read_bytes(), payload)

    def test_native_score_sysv_structures_lifecycle_and_poll_only_guest_delivery(self):
        _, events = self.run_native('--np-score-self-test')
        self.assertEqual(events[-1]['event'], 'np_score_self_test')
        self.assertTrue(events[-1]['passed'])
        self.assertGreaterEqual(events[-1]['cases'], 30)
        self.assertTrue(all(event.get('passed', True) for event in events))

    def test_native_scan_supports_the_actual_ranking_comment_integer_format(self):
        _, events = self.run_native('--scan-self-test')
        self.assertEqual(events[-1]['event'], 'scan_self_test')
        self.assertTrue(events[-1]['passed'])
        self.assertGreaterEqual(events[-1]['cases'], 16)
        self.assertTrue(all(event.get('passed', True) for event in events))

    def test_development_provider_state_and_callbacks_follow_actual_health_and_stop(self):
        entered, release = threading.Event(), threading.Event()
        class HeldHealth(live.Handler):
            def do_GET(self):
                if self.path == live.API + '/health':
                    entered.set()
                    release.wait(10)
                super().do_GET()
        with self.service(HeldHealth) as (_, config):
            process = subprocess.Popen([str(EXE), '--np-community-smoke', str(config), str(self.root / 'community-state')],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, encoding='utf-8', creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            prefix = []
            try:
                self.assertTrue(entered.wait(5), 'Actual worker never reached the held HTTP health request')
                for line in process.stdout:
                    prefix.append(line)
                    if json.loads(line).get('event') == 'np_community_before_health':
                        break
                else:
                    self.fail('Provider did not reach the pre-health barrier')
                release.set()
                stdout, stderr = process.communicate('!', timeout=12)
                events = [json.loads(line) for line in (''.join(prefix) + stdout).splitlines()]
                self.assertEqual(process.returncode, 0, ''.join(prefix) + stdout + stderr)
                self.assertEqual(events[-1]['event'], 'np_community_smoke')
                self.assertTrue(events[-1]['passed'])
                self.assertFalse(events[-1]['psn_authenticated'])
                transitions = [event['ready'] for event in events if event['event'] == 'native_community_state']
                self.assertEqual(transitions, [True, False])
                self.assertNotIn('Alice', ''.join(prefix) + stdout + stderr)
            finally:
                release.set()
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=5)

    def test_development_entry_rejects_public_endpoint_and_missing_explicit_profile_before_startup(self):
        config = self.root / 'shared.ini'
        original = (ROOT / 'config/input.ini').read_bytes()
        config.write_bytes(original)
        source = self.root / 'never-executed.ktprobe'
        source.write_bytes(b'authored invalid image')
        profile = self.root / 'must-not-exist'
        cases = [
            ['--enter', '--input-config', config, '--user-dir', profile, '--community-provider', 'loopback'],
            ['--enter', '--input-config', config, '--community-provider', 'loopback'],
            ['--validate', '--input-config', config, '--user-dir', profile, '--community-provider', 'loopback'],
            ['--enter', '--input-config', config, '--user-dir', profile, '--community-provider', 'public'],
            ['--enter', '--input-config', config, '--user-dir', profile, '--community-provider', 'configured'],
            ['--enter', '--input-config', config, '--user-dir', profile, '--community-provider', 'configured', '--community-rounds', 'on', '--community-round-audit', 'on'],
            ['--enter', '--community-round-audit', 'on'],
            ['--enter', '--community-rounds', 'on'],
            ['--validate', '--community-rounds', 'on'],
            ['--enter', '--input-config', config, '--user-dir', profile, '--community-provider', 'loopback', '--community-rounds', 'off'],
            ['--enter', '--input-config', config, '--user-dir', profile, '--community-provider', 'loopback', '--community-rounds', 'on'],
            ['--enter', '--input-config', config, '--user-dir', profile, '--community-provider', 'loopback', '--community-rounds', 'on', '--community-round-audit', 'on'],
            ['--validate', '--community-round-audit', 'on'],
            ['--enter', '--input-config', config, '--user-dir', profile, '--community-provider', 'loopback', '--community-round-audit', 'on'],
            ['--enter', '--input-config', config, '--user-dir', profile, '--community-provider', 'loopback', '--community-round-audit', 'off'],
        ]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                result = subprocess.run([str(EXE), str(source), *map(str, arguments)], cwd=self.root,
                                        capture_output=True, text=True, encoding='utf-8', timeout=10,
                                        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn('native_community_provider', result.stdout)
                self.assertNotIn('native_community_request', result.stdout)
                self.assertFalse(profile.exists())
                self.assertEqual(config.read_bytes(), original)
                self.assertEqual(source.read_bytes(), b'authored invalid image')

    def test_configured_service_disabled_creates_no_network_worker_and_enabled_requires_https(self):
        source = self.root / 'never-executed.ktprobe'
        source.write_bytes(b'authored invalid image')
        config = self.root / 'shared.ini'
        profile = self.root / 'isolated-profile'
        args = [source, '--enter', '--input-config', config, '--user-dir', profile,
                '--community-provider', 'configured', '--community-rounds', 'on']
        config.write_text('[keyboard]\n[community]\nenabled=false\n', encoding='utf-8')
        _, events = self.run_native(*args, expected=2)
        self.assertIn(dict(event='native_community_configuration', configured=True, enabled=False), events)
        self.assertFalse(any(e['event'] in ('native_community_request', 'native_community_round_producer_installed') for e in events))
        self.assertFalse((profile / 'community-rounds').exists())
        config.write_text('[keyboard]\n[community]\nenabled=true\nendpoint=http://127.0.0.1:1/ketsui/api/v1\n', encoding='utf-8')
        result, events = self.run_native(*args, expected=2)
        self.assertIn('requires HTTPS', result.stderr)
        self.assertFalse(any(e['event'] == 'native_community_request' for e in events))

    def test_native_score_provider_to_actual_local_service(self):
        with self.service() as (store, config):
            result, events = self.run_native('--np-score-smoke', config, self.root / 'score-client')
            self.assertTrue(events[-1]['passed'])
            self.assertFalse(events[-1]['native_menu_integration'])
            self.assertNotIn('Alice', result.stdout + result.stderr)
            page = store.leaderboard(0, 'standard')
            self.assertEqual(page['total'], 1)
            self.assertEqual(page['entries'][0]['score'], '9007199254740993')
            self.assertIsNone(page['entries'][0]['replay'])
            self.assertGreater(int(page['sorted_us']), 0)

    def test_native_replay_sysv_receipts_selection_and_chunk_delivery(self):
        _, events = self.run_native('--np-replay-self-test')
        self.assertEqual(events[-1]['event'], 'np_replay_self_test')
        self.assertTrue(events[-1]['passed'])
        self.assertFalse(events[-1]['native_playback_validated'])
        self.assertGreaterEqual(events[-1]['cases'], 24)
        self.assertTrue(all(event.get('passed', True) for event in events))

    def test_native_replay_provider_to_real_service_pins_entry_across_new_best(self):
        from test_community_replay import record
        payload = zlib.compress(record())
        path = self.root / '原生 ABI 自造附件'
        path.write_bytes(payload)
        with self.service() as (store, config):
            profile = self.root / 'native-replay-client'
            result, events = self.run_native('--np-replay-smoke', config, profile, path)
            self.assertEqual(events[-1]['event'], 'np_replay_smoke')
            self.assertTrue(events[-1]['passed'])
            self.assertFalse(events[-1]['native_menu_integration'])
            self.assertFalse(events[-1]['native_playback_validated'])
            self.assertEqual([(event['operation'], event['passed']) for event in events
                              if event['event'] == 'native_community_replay_validation'], [('upload', True), ('download', True), ('upload', False)])
            self.assertTrue(all(event['metadata_required'] for event in events if event['event'] == 'native_community_replay_validation'))
            validations = [event for event in events if event['event'] == 'native_community_replay_validation']
            self.assertEqual([event['reason'] for event in validations], [None, None, 'replay_score_mismatch'])
            self.assertTrue(all(event['game'] == 'ketsui' and event['payload_bytes'] == len(payload)
                                for event in validations))
            self.assertEqual([event['expected_score'] for event in validations], [9001, 9001, 9002])
            self.assertTrue(all(event['expected_board'] == 0 and event['header']['bytes'] == 0x80000
                                and event['header']['score'] == 9001 and event['header']['kind'] == 0
                                for event in validations))
            page = store.leaderboard(0, 'standard')
            self.assertEqual((page['total'], page['entries'][0]['score'], page['entries'][0]['replay']), (1, '9002', None))
            self.assertEqual(len(list((profile / 'community-outbox/.trash').rglob('request.json'))), 4)
            self.assertFalse(list((profile / 'community-outbox').glob('q*/request.json')))
            self.assertEqual(path.read_bytes(), payload)
            self.assertNotIn('Alice', result.stdout + result.stderr)

    def test_download_rejects_a_mismatched_digest_from_real_http(self):
        class CorruptDigest(live.Handler):
            def reply(self, status, value, *, binary_headers=None):
                if binary_headers:
                    binary_headers = dict(binary_headers, **{'X-Replay-SHA256': '0' * 64})
                super().reply(status, value, binary_headers=binary_headers)
        with self.service(CorruptDigest):
            _, events = self.run_native('--community-client-smoke', self.root / 'community.ini',
                                        self.root / 'bad-reply-client', expected=1)
            download = [event for event in events if event['event'] == 'native_community_request' and event['operation'] == 'download']
            self.assertTrue(download)
            self.assertEqual(download[-1]['error'], 5)  # Error::Protocol, not accepted bytes.

    def test_active_health_request_and_queued_work_are_cancelled_without_waiting_for_server(self):
        entered, release = threading.Event(), threading.Event()
        class HeldHealth(live.Handler):
            def do_GET(self):
                if self.path == live.API + '/health':
                    entered.set()
                    release.wait(10)
                super().do_GET()
        with self.service(HeldHealth) as (_, config):
            process = subprocess.Popen([str(EXE), '--community-client-cancel-smoke', str(config), str(self.root / 'cancel-client')],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, encoding='utf-8', creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            try:
                self.assertTrue(entered.wait(5), 'Server never admitted the actual client health request')
                stdout, stderr = process.communicate('!', timeout=8)
                self.assertEqual(process.returncode, 0, stdout + stderr)
                event = [json.loads(line) for line in stdout.splitlines() if 'community_client_cancel_smoke' in line][-1]
                self.assertTrue(event['passed'])
                self.assertLess(event['stop_ms'], 4500)
                self.assertEqual((event['cancelled'], event['queue_full']), (16, 2))
            finally:
                release.set()
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=5)

    def test_server_commit_then_client_kill_retries_same_submission_without_duplicates(self):
        committed, release = threading.Event(), threading.Event()
        class LostAcknowledgement(live.Handler):
            first = True
            def do_POST(self):
                if self.path == live.API + '/scores' and type(self).first:
                    type(self).first = False
                    body = self.read_json()
                    receipt = self.server.store.submit(body)
                    committed.set()
                    release.wait(10)
                    try:
                        self.reply(200, receipt)
                    except (ConnectionError, BrokenPipeError):
                        pass
                    return
                super().do_POST()
        with self.service(LostAcknowledgement) as (store, config):
            profile = self.root / 'crash-client'
            process = subprocess.Popen([str(EXE), '--community-client-smoke', str(config), str(profile)],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8',
                                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            try:
                self.assertTrue(committed.wait(5), 'No acknowledged server-side transaction to interrupt')
                process.kill()
                stdout, stderr = process.communicate(timeout=5)
                self.assertNotEqual(process.returncode, 0)
                records = list((profile / 'community-outbox').glob('q*/request.json'))
                self.assertEqual(len(records), 1)
                pending = json.loads(records[0].read_text(encoding='utf-8'))
                self.assertEqual(pending['state'], 'pending')
                original_submission = pending['body']['submission_id']
                release.set()
                _, events = self.run_native('--community-client-drain-smoke', config, profile)
                self.assertTrue(events[-1]['passed'])
                with store.connect() as db:
                    rows = db.execute('SELECT submission FROM scores').fetchall()
                self.assertEqual([row[0] for row in rows], [original_submission])
                self.assertFalse(list((profile / 'community-outbox').glob('q*/request.json')))
            finally:
                release.set()
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=5)

    def test_replay_commit_then_client_kill_recovers_exact_entry_and_bytes(self):
        committed, release = threading.Event(), threading.Event()
        received = []
        class LostReplayAcknowledgement(live.Handler):
            def do_POST(self):
                if self.path == live.API + '/replays':
                    body = self.read_json()
                    receipt = self.server.store.upload(body)
                    received.append(body)
                    if len(received) == 1:
                        committed.set()
                        release.wait(10)
                    try:
                        self.reply(200, receipt)
                    except (ConnectionError, BrokenPipeError):
                        pass
                    return
                super().do_POST()
        with self.service(LostReplayAcknowledgement) as (store, config):
            profile = self.root / 'replay-crash-client'
            process = subprocess.Popen([str(EXE), '--community-client-smoke', str(config), str(profile)],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8',
                                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            try:
                self.assertTrue(committed.wait(5), 'Replay did not commit before the interruption barrier')
                process.kill()
                process.communicate(timeout=5)
                self.assertNotEqual(process.returncode, 0)
                records = list((profile / 'community-outbox').glob('q*/request.json'))
                self.assertEqual(len(records), 1)
                pending = json.loads(records[0].read_text(encoding='utf-8'))
                self.assertEqual((pending['state'], pending['path']), ('pending', '/replays'))
                original = pending['body']
                self.assertEqual(original, received[0])
                release.set()
                _, events = self.run_native('--community-client-drain-smoke', config, profile)
                self.assertTrue(events[-1]['passed'])
                self.assertEqual(received, [original, original])
                with store.connect() as db:
                    scores = db.execute('SELECT id,submission FROM scores').fetchall()
                    replays = db.execute('SELECT entry_id,sha256,size,payload FROM replays').fetchall()
                    self.assertEqual(db.execute('PRAGMA integrity_check').fetchone()[0], 'ok')
                self.assertEqual([(r['id'], r['submission']) for r in scores],
                                 [(original['entry_id'], original['submission_id'])])
                self.assertEqual(len(replays), 1)
                replay = replays[0]
                payload = live.binary(original['payload_base64'], live.MAX_REPLAY)
                self.assertEqual((replay['entry_id'], replay['sha256'], replay['size'], replay['payload']),
                                 (original['entry_id'], original['sha256'], len(payload), payload))
                self.assertFalse(list((profile / 'community-outbox').glob('q*/request.json')))
                receipts = [json.loads(p.read_text(encoding='utf-8'))
                            for p in (profile / 'community-outbox/.trash').rglob('request.json')]
                self.assertEqual(len(receipts), 2)
                self.assertTrue(all(r['state'] == 'delivered' and 'body' not in r for r in receipts))
            finally:
                release.set()
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=5)


if __name__ == '__main__':
    unittest.main()
