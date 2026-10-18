"""Futari community scores/replays. Python 3.10+, SQLite, no account service.

Run behind an HTTPS reverse proxy. Bind only to loopback. Native view IDs are
opaque board keys: the client supplies the game's original column IDs/types.
All scores are immutable, nickname-grouped, and partitioned by ruleset.
"""
from __future__ import annotations

import argparse
import base64
from contextlib import contextmanager
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
from pathlib import Path
import re
import signal
import sqlite3
import struct
import threading
import time
import unicodedata
from urllib.parse import parse_qs, urlsplit
import uuid

API = "/futari/api/v1"
PROTOCOL = 1
TITLE_ID = "435A07D7"
MAX_REPLAY = 8 * 1024 * 1024
MAX_BODY = (MAX_REPLAY * 4 // 3) + 16384
MAX_SCORES = 100000
MAX_REPLAY_STORAGE = 512 * 1024 * 1024
RULESETS = {"standard", "training", "modified", "unknown"}
LOG = logging.getLogger("futari-live")


class ApiError(Exception):
    def __init__(self, status, code):
        self.status, self.code = status, code


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ApiError(400, "invalid_integer")
    return value


def text(value, limit):
    if not isinstance(value, str) or len(value) > limit:
        raise ApiError(400, "invalid_text")
    if any(unicodedata.category(c).startswith("C") for c in value):
        raise ApiError(400, "invalid_text")
    return value


def nickname(value):
    value = unicodedata.normalize("NFKC", text(value, 32)).strip()
    if not value or len(value.encode("utf-8")) > 48:
        raise ApiError(400, "invalid_nickname")
    return value


def ruleset(value):
    if value not in RULESETS:
        raise ApiError(400, "invalid_ruleset")
    return value


def identifier(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{32}", value):
        raise ApiError(400, "invalid_id")
    return value


def columns(value):
    if not isinstance(value, list) or len(value) > 64:
        raise ApiError(400, "invalid_columns")
    result, seen = [], set()
    for col in value:
        if not isinstance(col, dict) or set(col) != {"id", "type", "value"}:
            raise ApiError(400, "invalid_column")
        cid = integer(col["id"], 0, 0xffffffff)
        if cid in seen:
            raise ApiError(400, "duplicate_column")
        seen.add(cid)
        typ = integer(col["type"], 1, 7)
        v = col["value"]
        # XUSER_DATA numeric types used by the title. Keep 64-bit values as
        # decimal strings in JSON, avoiding JS double rounding on clients.
        if typ in (1, 2, 7):
            if isinstance(v, str) and re.fullmatch(r"-?[0-9]{1,20}", v):
                v = int(v)
            v = integer(v, -(1 << (31 if typ == 1 else 63)),
                        (1 << (31 if typ == 1 else 63)) - 1)
            v = str(v) if typ != 1 else v
        elif typ in (3, 5):
            if type(v) not in (int, float) or not -1e100 <= v <= 1e100:
                raise ApiError(400, "invalid_number")
        else:
            v = text(v, 1024)
        result.append({"id": cid, "type": typ, "value": v})
    return result


def validate_shared_replay(payload, fmt="futari-replaydata-v1"):
    """One canonical native slot, with the other 98 slots empty.

    This checks the file envelope/version, not the correctness of a run's
    score or simulation. Preserve the opaque native record byte-for-byte.
    """
    if fmt == "futari-xstorage-v1":
        # The title's network format is not a slot from the local save file.
        # Keep its 104-byte metadata header and original compressed stream.
        if not 104 < len(payload) <= 0xd30b8:
            raise ApiError(400, "invalid_replay_container")
        size, version = struct.unpack_from(">II", payload)
        if size != len(payload) or version != 0x09100800:
            raise ApiError(409, "incompatible_replay")
        return
    if fmt != "futari-replaydata-v1":
        raise ApiError(409, "incompatible_replay")
    table_size = 99 * 8
    if len(payload) < table_size + 0xa0 or len(payload) > MAX_REPLAY:
        raise ApiError(400, "invalid_replay_container")
    offset, size = struct.unpack_from(">II", payload)
    if offset != table_size or size != len(payload) - table_size:
        raise ApiError(400, "invalid_replay_container")
    for slot in range(1, 99):
        if struct.unpack_from(">II", payload, slot * 8) != (len(payload), 0):
            raise ApiError(400, "share_one_replay_slot")
    magic, _, length, nested_magic = struct.unpack_from(">IIII", payload, table_size)
    if magic != 0x09100800 or nested_magic != magic or length != size - 4:
        raise ApiError(409, "incompatible_replay")


class Store:
    def __init__(self, database):
        self.database = str(database)
        with self.connect() as db:
            db.executescript("""
              PRAGMA journal_mode=WAL;
              CREATE TABLE IF NOT EXISTS schema_version(version INTEGER NOT NULL);
              INSERT INTO schema_version SELECT 1 WHERE NOT EXISTS(SELECT 1 FROM schema_version);
              CREATE TABLE IF NOT EXISTS scores(
                id TEXT PRIMARY KEY, submission TEXT NOT NULL UNIQUE,
                digest TEXT NOT NULL, nickname TEXT NOT NULL,
                view INTEGER NOT NULL, rules TEXT NOT NULL, score INTEGER NOT NULL,
                columns_json TEXT NOT NULL, build TEXT NOT NULL,
                created INTEGER NOT NULL);
              CREATE INDEX IF NOT EXISTS board_order ON scores(view,rules,score DESC,created,id);
              CREATE INDEX IF NOT EXISTS player_board ON scores(nickname,view,rules,score DESC);
              CREATE TABLE IF NOT EXISTS replays(
                id TEXT PRIMARY KEY, score_id TEXT NOT NULL UNIQUE REFERENCES scores(id),
                sha256 TEXT NOT NULL, format TEXT NOT NULL, build TEXT NOT NULL,
                mode TEXT NOT NULL, size INTEGER NOT NULL, payload BLOB NOT NULL,
                created INTEGER NOT NULL);
            """)
            if db.execute("SELECT version FROM schema_version").fetchall() != [(1,)]:
                raise RuntimeError("Unsupported database schema")

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.database, timeout=5)
        db.row_factory = None
        db.execute("PRAGMA foreign_keys=ON")
        db.execute("PRAGMA busy_timeout=5000")
        try:
            with db:
                yield db
        finally:
            db.close()

    def submit(self, body):
        if not isinstance(body, dict):
            raise ApiError(400, "invalid_object")
        if body.get("title_id") != TITLE_ID or body.get("protocol") != PROTOCOL:
            raise ApiError(409, "incompatible_client")
        sub = identifier(body.get("submission"))
        name = nickname(body.get("nickname"))
        view = integer(body.get("view"), 0, 0xffffffff)
        rules = ruleset(body.get("rules"))
        score = body.get("score")
        if isinstance(score, str) and re.fullmatch(r"[0-9]{1,19}", score):
            score = int(score)
        score = integer(score, 0, (1 << 63) - 1)
        cols = columns(body.get("columns"))
        build = text(body.get("build"), 96)
        canonical = json.dumps([name, view, rules, score, cols, build],
                               ensure_ascii=False, separators=(",", ":"), sort_keys=True)
        digest = hashlib.sha256(canonical.encode()).hexdigest()
        sid = uuid.uuid4().hex
        with self.connect() as db:
            # BEGIN IMMEDIATE makes deduplication atomic across concurrent requests.
            db.execute("BEGIN IMMEDIATE")
            existing = db.execute("SELECT id,digest FROM scores WHERE submission=?", (sub,)).fetchone()
            if existing:
                if existing[1] != digest:
                    raise ApiError(409, "submission_conflict")
                return {"id": existing[0], "duplicate": True}
            if db.execute("SELECT COUNT(*) FROM scores").fetchone()[0] >= MAX_SCORES:
                raise ApiError(507, "score_storage_full")
            db.execute("INSERT INTO scores VALUES(?,?,?,?,?,?,?,?,?,?)",
                       (sid, sub, digest, name, view, rules, score,
                        json.dumps(cols, ensure_ascii=False), build, int(time.time())))
        return {"id": sid, "duplicate": False}

    def boards(self):
        with self.connect() as db:
            rows = db.execute("SELECT view,rules,COUNT(DISTINCT nickname) FROM scores "
                              "GROUP BY view,rules ORDER BY view,rules").fetchall()
        return {"boards": [{"view": v, "rules": r, "players": n} for v, r, n in rows]}

    def leaderboard(self, view, rules, start=1, count=20, around=None, names=None):
        view, rules = integer(view, 0, 0xffffffff), ruleset(rules)
        start, count = integer(start, 1, 1000000), integer(count, 1, 100)
        if around is not None:
            around = nickname(around)
        if names is not None:
            if len(names) > 100:
                raise ApiError(400, "too_many_names")
            names = [nickname(n) for n in names]
        # Pick each nickname's highest run, breaking ties by earliest submission.
        sql = """WITH personal AS(
          SELECT s.*,s.rowid AS sequence, ROW_NUMBER() OVER(PARTITION BY nickname ORDER BY score DESC,s.rowid) AS p
          FROM scores s WHERE view=? AND rules=?), ranked AS(
          SELECT *,ROW_NUMBER() OVER(ORDER BY score DESC,sequence) AS rank
          FROM personal WHERE p=1)
          SELECT r.id,r.nickname,r.score,r.columns_json,r.build,r.created,r.rank,
                 p.id AS replay_id,p.size,p.format FROM ranked r LEFT JOIN replays p ON p.score_id=r.id
        """
        with self.connect() as db:
            total = db.execute("SELECT COUNT(DISTINCT nickname) FROM scores WHERE view=? AND rules=?",
                               (view, rules)).fetchone()[0]
            if around:
                row = db.execute(sql + " WHERE r.nickname=?", (view, rules, around)).fetchone()
                if row:
                    start = max(1, row[6] - count // 2)
            args = [view, rules]
            if names is not None:
                if not names:
                    rows = []
                else:
                    rows = db.execute(sql + " WHERE r.nickname IN(" + ",".join("?" for _ in names)
                                      + ") ORDER BY r.rank", args + names).fetchall()
            else:
                rows = db.execute(sql + " WHERE r.rank>=? ORDER BY r.rank LIMIT ?",
                                  args + [start, count]).fetchall()
        return {"view": view, "rules": rules, "total": total, "start": start,
                "rows": [{"id": r[0], "nickname": r[1], "score": str(r[2]),
                          "columns": json.loads(r[3]), "build": r[4], "created": r[5],
                          "rank": r[6], "replay_id": r[7], "replay_size": r[8],
                          "replay_format": r[9]} for r in rows]}

    def estimate(self, view, rules, rating, name):
        view, rules = integer(view, 0, 0xffffffff), ruleset(rules)
        if not isinstance(rating, str) or not re.fullmatch(r"[0-9]{1,19}", rating):
            raise ApiError(400, "invalid_rating")
        rating, name = integer(int(rating), 0, (1 << 63) - 1), nickname(name)
        # Ranking is per nickname. A prospective improvement replaces this
        # nickname's old best; existing equal scores precede a new submission.
        with self.connect() as db:
            rank = db.execute("SELECT COUNT(*)+1 FROM (SELECT nickname,MAX(score) AS best "
                              "FROM scores WHERE view=? AND rules=? AND nickname!=? "
                              "GROUP BY nickname) WHERE best>=?",
                              (view, rules, name, rating)).fetchone()[0]
        return {"view": view, "rules": rules, "rank": rank}

    def upload_replay(self, body):
        if not isinstance(body, dict):
            raise ApiError(400, "invalid_object")
        if body.get("title_id") != TITLE_ID or body.get("protocol") != PROTOCOL:
            raise ApiError(409, "incompatible_client")
        sid, name = identifier(body.get("score_id")), nickname(body.get("nickname"))
        fmt = text(body.get("format"), 64)
        if fmt not in ("futari-replaydata-v1", "futari-xstorage-v1"):
            raise ApiError(409, "incompatible_replay")
        build, mode = text(body.get("build"), 96), text(body.get("mode"), 64)
        sha = body.get("sha256")
        if not isinstance(sha, str) or not re.fullmatch("[0-9a-f]{64}", sha):
            raise ApiError(400, "invalid_hash")
        encoded = body.get("payload")
        if not isinstance(encoded, str) or len(encoded) > (MAX_REPLAY + 2) // 3 * 4:
            raise ApiError(413, "replay_too_large")
        try:
            payload = base64.b64decode(encoded, validate=True)
        except ValueError:
            raise ApiError(400, "invalid_base64") from None
        if not 1 <= len(payload) <= MAX_REPLAY or hashlib.sha256(payload).hexdigest() != sha:
            raise ApiError(400, "invalid_replay_payload")
        validate_shared_replay(payload, fmt)
        rid = uuid.uuid4().hex
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            row = db.execute("SELECT nickname,build FROM scores WHERE id=?", (sid,)).fetchone()
            if not row:
                raise ApiError(404, "score_not_found")
            if row != (name, build):
                raise ApiError(409, "replay_score_mismatch")
            existing = db.execute("SELECT id,sha256,format,mode FROM replays WHERE score_id=?",
                                  (sid,)).fetchone()
            if existing:
                if existing[1:] != (sha, fmt, mode):
                    raise ApiError(409, "replay_already_attached")
                return {"id": existing[0], "duplicate": True}
            used = db.execute("SELECT COALESCE(SUM(size),0) FROM replays").fetchone()[0]
            if used + len(payload) > MAX_REPLAY_STORAGE:
                raise ApiError(507, "replay_storage_full")
            db.execute("INSERT INTO replays VALUES(?,?,?,?,?,?,?,?,?)",
                       (rid, sid, sha, fmt, build, mode, len(payload), payload, int(time.time())))
        return {"id": rid, "duplicate": False}

    def replay(self, rid):
        with self.connect() as db:
            row = db.execute("SELECT score_id,sha256,format,build,mode,size,payload FROM replays WHERE id=?",
                             (identifier(rid),)).fetchone()
        if not row:
            raise ApiError(404, "replay_not_found")
        return {"id": rid, "title_id": TITLE_ID, "protocol": PROTOCOL, "score_id": row[0],
                "sha256": row[1], "format": row[2], "build": row[3], "mode": row[4],
                "size": row[5], "payload": base64.b64encode(row[6]).decode("ascii")}


class Server(ThreadingHTTPServer):
    daemon_threads = True
    block_on_close = True
    request_queue_size = 32

    def __init__(self, address, store):
        self.store = store
        # No IP/user-agent/nickname access logs. nginx also disables access logs
        # for this API. This is a connection bound, not an identity/account limit.
        self.slots = threading.BoundedSemaphore(4)
        super().__init__(address, Handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(False):
            request.close()
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()


class Handler(BaseHTTPRequestHandler):
    server_version = "FutariLive/1"
    sys_version = ""

    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, fmt, *args):
        pass

    def reply(self, status, body):
        data = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.handle_api(False)

    def do_POST(self):
        self.handle_api(True)

    def handle_api(self, post):
        request_id = uuid.uuid4().hex[:12]
        started = time.monotonic()
        route = "unknown"
        try:
            url = urlsplit(self.path)
            if not url.path.startswith(API + "/") or len(self.path) > 8192:
                raise ApiError(404, "not_found")
            route = url.path[len(API):]
            query = parse_qs(url.query, max_num_fields=110, strict_parsing=True) if url.query else {}

            def one(key, default=None):
                values = query.get(key, [default])
                if len(values) != 1:
                    raise ApiError(400, "duplicate_parameter")
                return values[0]

            body = None
            if post:
                if self.headers.get("Transfer-Encoding"):
                    raise ApiError(400, "unsupported_transfer")
                if self.headers.get_content_type() != "application/json":
                    raise ApiError(415, "json_required")
                try:
                    length = int(self.headers.get("Content-Length", "-1"))
                except ValueError:
                    raise ApiError(400, "invalid_length") from None
                cap = MAX_BODY if route == "/replays" else 65536
                if not 0 < length <= cap:
                    raise ApiError(413, "body_too_large")
                raw = self.rfile.read(length)
                if len(raw) != length:
                    raise ApiError(400, "incomplete_body")
                body = json.loads(raw)
            store = self.server.store
            if not post and route == "/health":
                result = {"status": "ok", "protocol": PROTOCOL, "title_id": TITLE_ID,
                          "features": ["scores", "leaderboards", "replays"], "max_replay_bytes": MAX_REPLAY}
            elif not post and route == "/boards":
                result = store.boards()
            elif not post and route == "/leaderboard":
                result = store.leaderboard(int(one("view")), one("rules", "standard"),
                                           int(one("start", "1")), int(one("count", "20")),
                                           one("around"), query.get("name"))
            elif not post and route == "/estimate":
                result = store.estimate(int(one("view")), one("rules", "standard"),
                                        one("score"), one("name"))
            elif post and route == "/scores":
                result = store.submit(body)
            elif post and route == "/replays":
                result = store.upload_replay(body)
            elif not post and route.startswith("/replays/"):
                result = store.replay(route[len("/replays/"):])
                route = "/replays/:id"
            else:
                raise ApiError(404, "not_found")
            self.reply(200, result)
            LOG.info("request=%s route=%s status=200 ms=%d", request_id, route,
                     (time.monotonic() - started) * 1000)
        except ApiError as exc:
            self.reply(exc.status, {"error": exc.code, "request": request_id})
            LOG.info("request=%s status=%d error=%s", request_id, exc.status, exc.code)
        except (ValueError, TypeError, UnicodeError, RecursionError):
            self.reply(400, {"error": "invalid_request", "request": request_id})
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            LOG.info("request=%s disconnected", request_id)
        except Exception:
            # Do not log request bodies, replay bytes, nicknames, IPs or URLs.
            LOG.exception("request=%s internal_error", request_id)
            self.reply(500, {"error": "internal_error", "request": request_id})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18084)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    server = Server(("127.0.0.1", args.port), Store(args.database))
    def shutdown(_sig, _frame):
        threading.Thread(target=server.shutdown, daemon=True).start()
    signal.signal(signal.SIGTERM, shutdown)
    signal.signal(signal.SIGINT, shutdown)
    LOG.info("ready protocol=%d port=%d", PROTOCOL, args.port)
    try:
        server.serve_forever(poll_interval=0.5)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
