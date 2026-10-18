"""Run: python -m unittest discover -s tests -p test_live_server.py -v"""
import base64
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import struct
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen
import uuid
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("futari_live", Path(__file__).resolve().parents[1] / "server/games/futari.py")
live = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)
backup_spec = importlib.util.spec_from_file_location("futari_backup", Path(__file__).resolve().parents[1] / "server/backup.py")
backup_module = importlib.util.module_from_spec(backup_spec)
backup_spec.loader.exec_module(backup_module)


def sample_replay():
    # Synthetic format fixture, not translated game code or a playable run.
    record = bytearray(160)
    struct.pack_into(">IIII", record, 0, 0x09100800, 0, 156, 0x09100800)
    return struct.pack(">II", 792, 160) + struct.pack(">II", 952, 0) * 98 + record


class LiveTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.store = live.Store(Path(self.tmp.name) / "test.sqlite3")

    def tearDown(self):
        self.tmp.cleanup()

    def score(self, name="测试水母-A7K2", score=1234, view=7, rules="standard"):
        return {"protocol": 1, "title_id": live.TITLE_ID, "submission": uuid.uuid4().hex,
                "nickname": name, "view": view, "rules": rules, "score": str(score),
                "columns": [{"id": 1, "type": 2, "value": str(score)}], "build": "test-build"}

    def test_best_paging_around_me_ties_and_rules(self):
        for n, s in (("Alice", 500), ("Alice", 700), ("Bob", 900), ("Cat", 700)):
            self.store.submit(self.score(n, s))
        self.store.submit(self.score("NotRanked", 9999, rules="training"))
        self.store.submit(self.score("OtherMode", 9999, view=8))
        page = self.store.leaderboard(7, "standard", count=2)
        self.assertEqual(page["total"], 3)
        self.assertEqual([r["score"] for r in page["rows"]], ["900", "700"])
        self.assertEqual([r["nickname"] for r in self.store.leaderboard(7, "standard")["rows"]],
                         ["Bob", "Alice", "Cat"])
        self.assertEqual(len(self.store.leaderboard(7, "standard", start=3)["rows"]), 1)
        around = self.store.leaderboard(7, "standard", count=1, around="Cat")
        self.assertEqual(around["rows"][0]["nickname"], "Cat")
        self.assertEqual(self.store.leaderboard(7, "standard", names=["Alice"])["rows"][0]["score"], "700")
        self.assertEqual(self.store.leaderboard(99, "standard")["rows"], [])

    def test_atomic_dedup_and_conflict(self):
        score = self.score()
        with ThreadPoolExecutor(8) as pool:
            results = list(pool.map(lambda _: self.store.submit(score), range(16)))
        self.assertEqual(len({r["id"] for r in results}), 1)
        self.assertEqual(sum(not r["duplicate"] for r in results), 1)
        score["score"] = "999"
        with self.assertRaises(live.ApiError) as ctx:
            self.store.submit(score)
        self.assertEqual(ctx.exception.status, 409)

    def test_estimated_rank(self):
        self.assertEqual(self.store.estimate(7, "standard", "500", "New")["rank"], 1)
        for name, score in (("Alice", 500), ("Alice", 700), ("Bob", 900), ("Cat", 700)):
            self.store.submit(self.score(name, score))
        self.store.submit(self.score("Elsewhere", 9999, view=8))
        self.store.submit(self.score("Training", 9999, rules="training"))
        self.assertEqual(self.store.estimate(7, "standard", "700", "New")["rank"], 4)
        self.assertEqual(self.store.estimate(7, "standard", "700", "Alice")["rank"], 3)
        self.assertEqual(self.store.estimate(7, "standard", "901", "Alice")["rank"], 1)
        self.assertEqual(self.store.estimate(7, "standard", str(2**63-1), "New")["rank"], 1)
        for bad in ("-1", "1e3", "1.5", str(2**63)):
            with self.assertRaises(live.ApiError):
                self.store.estimate(7, "standard", bad, "New")

    def test_native_storage_replay_envelope(self):
        payload = bytearray(160)
        struct.pack_into(">II", payload, 0, len(payload), 0x09100800)
        live.validate_shared_replay(payload, "futari-xstorage-v1")
        sid = self.store.submit(self.score())["id"]
        replay = {"protocol": 1, "title_id": live.TITLE_ID, "score_id": sid,
                  "nickname": "测试水母-A7K2", "format": "futari-xstorage-v1",
                  "build": "test-build", "mode": "view-7",
                  "sha256": hashlib.sha256(payload).hexdigest(),
                  "payload": base64.b64encode(payload).decode()}
        rid = self.store.upload_replay(replay)["id"]
        self.assertEqual(base64.b64decode(self.store.replay(rid)["payload"]), payload)
        row = self.store.leaderboard(7, "standard")["rows"][0]
        self.assertEqual(row["replay_size"], len(payload))
        self.assertEqual(row["replay_format"], "futari-xstorage-v1")
        for offset in (0, 4):
            invalid = bytearray(payload)
            invalid[offset] ^= 0xff
            with self.assertRaises(live.ApiError):
                live.validate_shared_replay(invalid, "futari-xstorage-v1")
        for invalid in (payload[:104], bytearray(0xd30b9), sample_replay()):
            with self.assertRaises(live.ApiError):
                live.validate_shared_replay(invalid, "futari-xstorage-v1")
        with self.assertRaises(live.ApiError):
            live.validate_shared_replay(payload, "futari-replaydata-v1")

    def test_precision_normalization_and_injection(self):
        self.store.submit(self.score("  Ａlice  ", 2**63 - 1))
        row = self.store.leaderboard(7, "standard", names=["Alice"])["rows"][0]
        self.assertEqual(row["score"], str(2**63 - 1))
        self.store.submit(self.score("'; DROP TABLE scores;--", 20))
        self.assertEqual(self.store.leaderboard(7, "standard")["total"], 2)
        for bad in ("", "\nBob", "a\x00b", "x" * 33):
            with self.assertRaises(live.ApiError):
                self.store.submit(self.score(bad))

    def test_replay_roundtrip_immutable_and_hash(self):
        score = self.score()
        sid = self.store.submit(score)["id"]
        payload = sample_replay()
        replay = {"protocol": 1, "title_id": live.TITLE_ID, "score_id": sid,
                  "nickname": score["nickname"], "format": "futari-replaydata-v1",
                  "build": "test-build", "mode": "test-mode", "sha256": hashlib.sha256(payload).hexdigest(),
                  "payload": base64.b64encode(payload).decode()}
        rid = self.store.upload_replay(replay)["id"]
        self.assertTrue(self.store.upload_replay(replay)["duplicate"])
        download = self.store.replay(rid)
        self.assertEqual(base64.b64decode(download["payload"]), payload)
        self.assertEqual(self.store.leaderboard(7, "standard")["rows"][0]["replay_id"], rid)
        replay["sha256"] = "0" * 64
        with self.assertRaises(live.ApiError):
            self.store.upload_replay(replay)
        replay["sha256"] = hashlib.sha256(payload).hexdigest()
        replay["build"] = "wrong-build"
        with self.assertRaises(live.ApiError):
            self.store.upload_replay(replay)
        with self.assertRaises(live.ApiError):
            self.store.replay("../file")

    def test_invalid_values(self):
        for key, value in (("view", -1), ("view", True), ("score", "1e100"), ("score", str(2**63)),
                           ("rules", "anything"), ("protocol", 2), ("columns", [1])):
            s = self.score()
            s[key] = value
            with self.assertRaises(live.ApiError):
                self.store.submit(s)

    def test_storage_caps_do_not_break_reads_or_retries(self):
        score = self.score()
        sid = self.store.submit(score)["id"]
        with patch.object(live, "MAX_SCORES", 1):
            self.assertTrue(self.store.submit(score)["duplicate"])
            with self.assertRaises(live.ApiError) as ctx:
                self.store.submit(self.score("Second"))
            self.assertEqual(ctx.exception.status, 507)
        payload = sample_replay()
        replay = {"protocol": 1, "title_id": live.TITLE_ID, "score_id": sid, "nickname": score["nickname"],
                  "format": "futari-replaydata-v1", "build": "test-build", "mode": "test",
                  "sha256": hashlib.sha256(payload).hexdigest(), "payload": base64.b64encode(payload).decode()}
        with patch.object(live, "MAX_REPLAY_STORAGE", 1):
            with self.assertRaises(live.ApiError) as ctx:
                self.store.upload_replay(replay)
            self.assertEqual(ctx.exception.status, 507)
        self.assertEqual(self.store.leaderboard(7, "standard")["total"], 1)

    def test_online_backup_and_retention(self):
        self.store.submit(self.score())
        directory = Path(self.tmp.name) / "backups"
        for _ in range(9):
            snapshot = backup_module.backup(self.store.database, directory)
        self.assertEqual(len(list(directory.glob("*.sqlite3"))), 7)
        restored = live.Store(snapshot)
        self.assertEqual(restored.leaderboard(7, "standard")["total"], 1)

    def test_replay_envelope_validation(self):
        live.validate_shared_replay(sample_replay())
        for bad in (b"test", sample_replay()[:-1], b"\xff"+sample_replay()[1:]):
            with self.assertRaises(live.ApiError):
                live.validate_shared_replay(bad)
        data = bytearray(sample_replay())
        struct.pack_into(">I", data, 8, 800)
        with self.assertRaises(live.ApiError):
            live.validate_shared_replay(data)

    def test_http_contract_and_restart(self):
        server = live.Server(("127.0.0.1", 0), self.store)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        url = "http://127.0.0.1:" + str(server.server_port) + live.API
        def request(path, body=None, headers=None):
            req = Request(url + path, data=None if body is None else body,
                          headers=headers or {"Content-Type": "application/json"})
            return urlopen(req, timeout=3)
        try:
            self.assertEqual(json.load(request("/health"))["protocol"], 1)
            score = self.score()
            sid = json.load(request("/scores", json.dumps(score).encode()))["id"]
            self.assertEqual(json.load(request("/leaderboard?view=7"))["rows"][0]["id"], sid)
            self.assertEqual(json.load(request("/estimate?view=7&score=100&name=New"))["rank"], 2)
            self.assertEqual(json.load(request("/estimate?view=7&score=9999&name=New"))["rank"], 1)
            for path, body, status in (("/missing", None, 404), ("/scores", b"{", 400),
                                       ("/scores", b"[]", 400), ("/leaderboard?view=-1", None, 400),
                                       ("/leaderboard?view=7&view=8", None, 400)):
                with self.assertRaises(HTTPError) as ctx:
                    request(path, body)
                self.assertEqual(ctx.exception.code, status)
                self.assertIn("error", json.load(ctx.exception))
            restored = live.Store(self.store.database)
            self.assertEqual(restored.leaderboard(7, "standard")["total"], 1)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(3)


if __name__ == "__main__":
    unittest.main()
