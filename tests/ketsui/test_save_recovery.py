"""Interruption tests operate only on marked, authored temporary save slots."""
import hashlib
import json
import os
import queue
import subprocess
import tempfile
import threading
import unittest
from contextlib import contextmanager
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'out/native/KetsuiNativeProbe.exe'


@unittest.skipUnless(os.name == 'nt' and EXE.is_file(), 'Build the native Windows probe first')
class SaveRecoveryTests(unittest.TestCase):
    def invoke(self, directory, phase):
        result = subprocess.run([str(EXE), '--save-crash-fixture', str(directory), phase],
                                capture_output=True, text=True, timeout=15, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return [json.loads(line) for line in result.stdout.splitlines()]

    @contextmanager
    def parked_writer(self, directory, phase):
        child = subprocess.Popen([str(EXE), '--save-crash-fixture', str(directory), phase],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT)
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
                self.assertIsNotNone(line, 'Writer exited before its checkpoint')
                event = json.loads(line)
                if event.get('event') == 'save_crash_fixture_checkpoint':
                    self.assertEqual(event['point'], phase)
                    self.assertIsNone(child.poll())
                    break
            yield child
        finally:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)
            reader.join(timeout=5)
            self.assertFalse(reader.is_alive(), 'Fixture stdout reader did not stop')
            child.stdout.close()
            child.stderr.close()

    @staticmethod
    def save_root(directory):
        return Path(directory) / 'profiles/1000/CUSA09062/savedata'

    @staticmethod
    def hashes(slot):
        return {str(path.relative_to(slot)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in slot.rglob('*') if path.is_file()}

    def test_interrupted_writes_keep_complete_old_or_complete_new_generation(self):
        for phase in ('partial', 'closed', 'ready', 'previous_moved', 'published'):
            with self.subTest(phase=phase), tempfile.TemporaryDirectory(prefix='kt-save-crash-') as directory:
                self.invoke(directory, 'seed')
                root = self.save_root(directory)
                slot = root / 'Fixture'
                (slot / 'untouched.bin').write_bytes(b'Unchanged file survives complete generation commit')
                before = self.hashes(slot)
                with self.parked_writer(directory, phase) as child:
                    # The exact production commit checkpoint is confirmed live
                    # before this test terminates only its own child process.
                    child.kill()
                    child.wait(timeout=5)
                events = self.invoke(directory, 'inspect')
                inspected = events[-1]
                committed = phase in ('ready', 'previous_moved', 'published')
                self.assertEqual(inspected['new_payload'], committed)
                self.assertEqual(inspected['new_title'], committed)
                self.assertEqual(inspected['old_payload'], not committed)
                self.assertEqual(inspected['old_title'], not committed)
                self.assertEqual((slot / 'data.bin').read_bytes(), (b'B' if committed else b'A') * 65536)
                self.assertEqual((slot / 'second.bin').read_bytes(), (b'D' if committed else b'C') * 32768)
                self.assertEqual((slot / 'untouched.bin').read_bytes(), b'Unchanged file survives complete generation commit')
                self.assertEqual((slot / 'sce_sys/icon0.png').read_bytes(), b'\x89PNG\r\n\x1a' + (b'\x0b' if committed else b'\x0a'))
                if not committed:
                    self.assertEqual(self.hashes(slot), before)
                    self.assertTrue(any((root / '.kt-recovery').iterdir()))
                self.assertEqual(list((root / '.kt-transactions').iterdir()), [])
                after = self.hashes(slot)
                self.invoke(directory, 'inspect')
                self.assertEqual(self.hashes(slot), after, 'Recovery is not idempotent')

    def test_interrupted_first_creation_is_absent_or_a_complete_slot(self):
        for phase in ('new_partial', 'new_closed', 'new_ready', 'new_published'):
            with self.subTest(phase=phase), tempfile.TemporaryDirectory(prefix='kt-save-new-crash-') as directory:
                self.invoke(directory, 'seed')
                root = self.save_root(directory)
                before = self.hashes(root / 'Fixture')
                with self.parked_writer(directory, phase):
                    pass
                self.invoke(directory, 'inspect')
                committed = phase in ('new_ready', 'new_published')
                self.assertEqual((root / 'Fresh').exists(), committed)
                if committed:
                    inspected = self.invoke(directory, 'inspect_fresh')[-1]
                    self.assertTrue(inspected['new_payload'])
                    self.assertTrue(inspected['new_title'])
                    self.assertEqual((root / 'Fresh/second.bin').read_bytes(), b'D' * 32768)
                    self.assertEqual((root / 'Fresh/sce_sys/icon0.png').read_bytes(), b'\x89PNG\r\n\x1a\x0b')
                self.assertEqual(self.hashes(root / 'Fixture'), before)
                self.assertEqual(list((root / '.kt-transactions').iterdir()), [])

    def test_recovery_can_itself_be_interrupted_and_repeated(self):
        for phase in ('recover_previous_moved', 'recover_published'):
            with self.subTest(phase=phase), tempfile.TemporaryDirectory(prefix='kt-save-recover-crash-') as directory:
                self.invoke(directory, 'seed')
                with self.parked_writer(directory, 'ready'):
                    pass
                with self.parked_writer(directory, phase):
                    pass
                inspected = self.invoke(directory, 'inspect')[-1]
                self.assertTrue(inspected['new_payload'])
                self.assertTrue(inspected['new_title'])
                slot = self.save_root(directory) / 'Fixture'
                self.assertEqual((slot / 'second.bin').read_bytes(), b'D' * 32768)
                before = self.hashes(slot)
                self.invoke(directory, 'inspect')
                self.assertEqual(self.hashes(slot), before)

    def test_an_active_writer_is_not_recovered_by_another_process(self):
        with tempfile.TemporaryDirectory(prefix='kt-save-live-writer-') as directory:
            self.invoke(directory, 'seed')
            slot = self.save_root(directory) / 'Fixture'
            before = self.hashes(slot)
            with self.parked_writer(directory, 'partial') as child:
                result = subprocess.run([str(EXE), '--save-crash-fixture', directory, 'inspect'],
                                        capture_output=True, text=True, timeout=10, cwd=ROOT)
                self.assertEqual(result.returncode, 2)
                self.assertIsNone(child.poll())
                self.assertEqual(self.hashes(slot), before)
            self.assertTrue(self.invoke(directory, 'inspect')[-1]['old_payload'])

    def test_abandoned_staging_without_a_record_never_replaces_a_slot(self):
        for phase in ('partial', 'new_partial'):
            with self.subTest(phase=phase), tempfile.TemporaryDirectory(prefix='kt-save-no-record-') as directory:
                self.invoke(directory, 'seed')
                root = self.save_root(directory)
                before = self.hashes(root / 'Fixture')
                with self.parked_writer(directory, phase):
                    pass
                key = 'fresh' if phase.startswith('new_') else 'fixture'
                # Model the interval after work-directory creation and before
                # the first atomic staging record, using only owned test data.
                (root / '.kt-transactions' / key / 'state.bin').unlink()
                self.assertTrue(self.invoke(directory, 'inspect')[-1]['old_payload'])
                self.assertEqual(self.hashes(root / 'Fixture'), before)
                self.assertFalse((root / 'Fresh').exists())
                self.assertEqual(list((root / '.kt-transactions').iterdir()), [])
                self.assertTrue(any((root / '.kt-recovery').iterdir()))

    def test_corrupt_commit_record_does_not_guess_or_replace_the_original(self):
        for change in ('checksum', 'slot_name'):
            with self.subTest(change=change), tempfile.TemporaryDirectory(prefix='kt-save-bad-record-') as directory:
                self.invoke(directory, 'seed')
                root = self.save_root(directory)
                before = self.hashes(root / 'Fixture')
                with self.parked_writer(directory, 'ready'):
                    pass
                record_path = root / '.kt-transactions/fixture/state.bin'
                record = bytearray(record_path.read_bytes())
                if change == 'checksum':
                    record[120] ^= 1
                else:
                    record[16:48] = b'../escape\0'.ljust(32, b'\0')
                    record[128:] = hashlib.sha256(record[:128]).digest()
                record_path.write_bytes(record)
                result = subprocess.run([str(EXE), '--save-crash-fixture', directory, 'inspect'],
                                        capture_output=True, text=True, timeout=10, cwd=ROOT)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(self.hashes(root / 'Fixture'), before)
                self.assertTrue(record_path.is_file())
                self.assertEqual((root / '.kt-transactions/fixture/work/data.bin').read_bytes(), b'B' * 65536)
                self.assertFalse((root / 'escape').exists())

    def test_changed_work_directory_identity_is_not_published(self):
        with tempfile.TemporaryDirectory(prefix='kt-save-bad-identity-') as directory:
            self.invoke(directory, 'seed')
            root = self.save_root(directory)
            before = self.hashes(root / 'Fixture')
            with self.parked_writer(directory, 'ready'):
                pass
            work = root / '.kt-transactions/fixture/work'
            work.rename(root / '.kt-authored-held-work')
            work.mkdir()
            sentinel = work / 'unrelated.bin'
            sentinel.write_bytes(b'Unrelated authored directory, do not adopt')
            result = subprocess.run([str(EXE), '--save-crash-fixture', directory, 'inspect'],
                                    capture_output=True, text=True, timeout=10, cwd=ROOT)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(self.hashes(root / 'Fixture'), before)
            self.assertEqual(sentinel.read_bytes(), b'Unrelated authored directory, do not adopt')
            self.assertEqual((root / '.kt-authored-held-work/data.bin').read_bytes(), b'B' * 65536)

    def test_changed_transaction_container_is_not_cleaned_or_adopted(self):
        with tempfile.TemporaryDirectory(prefix='kt-save-bad-container-') as directory:
            self.invoke(directory, 'seed')
            root = self.save_root(directory)
            before = self.hashes(root / 'Fixture')
            with self.parked_writer(directory, 'ready'):
                pass
            transaction = root / '.kt-transactions/fixture'
            held = root / '.kt-authored-held-transaction'
            transaction.rename(held)
            transaction.mkdir()
            record = transaction / 'state.bin'
            record.write_bytes((held / 'state.bin').read_bytes())
            result = subprocess.run([str(EXE), '--save-crash-fixture', directory, 'inspect'],
                                    capture_output=True, text=True, timeout=10, cwd=ROOT)
            self.assertEqual(result.returncode, 2)
            self.assertIn('container identity changed', result.stderr)
            self.assertEqual(self.hashes(root / 'Fixture'), before)
            self.assertTrue(record.is_file())
            self.assertEqual((held / 'work/data.bin').read_bytes(), b'B' * 65536)

    def test_flush_error_poisoning_prevents_later_publication(self):
        with tempfile.TemporaryDirectory(prefix='kt-save-flush-failure-') as directory:
            self.invoke(directory, 'seed')
            slot = self.save_root(directory) / 'Fixture'
            before = self.hashes(slot)
            result = self.invoke(directory, 'flush_error')[-1]
            self.assertTrue(result['flush_failed'])
            self.assertTrue(result['commit_blocked'])
            self.assertEqual(self.hashes(slot), before)
            self.assertTrue(self.invoke(directory, 'inspect')[-1]['old_payload'])
            self.assertEqual(self.hashes(slot), before)

    def test_prepare_and_publication_sharing_failures_can_be_retried(self):
        for phase in ('prepare_retry', 'publication_retry'):
            with self.subTest(phase=phase), tempfile.TemporaryDirectory(prefix='kt-save-retry-') as directory:
                self.invoke(directory, 'seed')
                retried = self.invoke(directory, phase)[-1]
                self.assertTrue(all(retried[key] for key in ('initially_blocked', 'mutation_blocked', 'retry_committed')))
                self.assertIn(retried['failure_win32'], (5, 32, 33))
                inspected = self.invoke(directory, 'inspect')[-1]
                self.assertTrue(inspected['new_payload'])
                self.assertTrue(inspected['new_title'])
                root = self.save_root(directory)
                self.assertEqual((root / 'Fixture/second.bin').read_bytes(), b'D' * 32768)
                self.assertEqual(list((root / '.kt-transactions').iterdir()), [])

    def test_cleanup_failure_keeps_the_published_save_and_recovers_later(self):
        with tempfile.TemporaryDirectory(prefix='kt-save-cleanup-') as directory:
            self.invoke(directory, 'seed')
            events = self.invoke(directory, 'cleanup_pending')
            self.assertTrue(events[-1]['cleanup_pending'])
            self.assertTrue(any(event['event'] == 'native_save_transaction_cleanup_pending' for event in events))
            root = self.save_root(directory)
            self.assertEqual((root / 'Fixture/data.bin').read_bytes(), b'B' * 65536)
            before = self.hashes(root / 'Fixture')
            inspected = self.invoke(directory, 'inspect')[-1]
            self.assertTrue(inspected['new_payload'])
            self.assertTrue(inspected['new_title'])
            self.assertEqual(self.hashes(root / 'Fixture'), before)
            self.assertEqual(list((root / '.kt-transactions').iterdir()), [])

    def test_failed_staging_preserves_the_original_and_does_not_leak_copies(self):
        with tempfile.TemporaryDirectory(prefix='kt-save-stage-failure-') as directory:
            self.invoke(directory, 'seed')
            root = self.save_root(directory)
            slot = root / 'Fixture'
            (slot / 'authored-over-quota.bin').write_bytes(b'Q' * (4 * 1024 * 1024))
            before = self.hashes(slot)
            for _ in range(2):
                result = subprocess.run([str(EXE), '--save-crash-fixture', directory, 'partial'],
                                        capture_output=True, text=True, timeout=10, cwd=ROOT)
                self.assertEqual(result.returncode, 2)
                self.assertIn('quota', result.stderr)
                self.assertEqual(self.hashes(slot), before)
                self.assertEqual(list((root / '.kt-transactions').iterdir()), [])
                self.assertFalse((root / '.kt-recovery').exists())
            self.assertTrue(self.invoke(directory, 'inspect')[-1]['old_payload'])
            self.assertEqual(self.hashes(slot), before)

    def test_prepared_tree_flush_failure_never_issues_a_commit(self):
        with tempfile.TemporaryDirectory(prefix='kt-save-tree-flush-') as directory:
            self.invoke(directory, 'seed')
            root = self.save_root(directory)
            slot = root / 'Fixture'
            (slot / 'untouched.bin').write_bytes(b'Flush this unchanged copy before preparing')
            before = self.hashes(slot)
            events = self.invoke(directory, 'prepare_flush_error')
            self.assertTrue(events[-1]['commit_blocked'])
            self.assertFalse(any(event['event'] == 'native_save_transaction_completed' for event in events))
            self.assertEqual(self.hashes(slot), before)
            record = (root / '.kt-transactions/fixture/state.bin').read_bytes()
            self.assertEqual(int.from_bytes(record[12:16], 'little'), 0)
            self.assertTrue(self.invoke(directory, 'inspect')[-1]['old_payload'])
            self.assertEqual(self.hashes(slot), before)

    def test_fixture_refuses_unmarked_or_nonempty_user_directories(self):
        with tempfile.TemporaryDirectory(prefix='kt-save-marker-') as directory:
            sentinel = Path(directory) / 'personal-sentinel.bin'
            sentinel.write_bytes(b'untouched')
            for phase in ('seed', 'partial', 'ready', 'inspect'):
                result = subprocess.run([str(EXE), '--save-crash-fixture', directory, phase],
                                        capture_output=True, text=True, timeout=10, cwd=ROOT)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(sentinel.read_bytes(), b'untouched')
                self.assertEqual(list(Path(directory).iterdir()), [sentinel])


if __name__ == '__main__':
    unittest.main()
