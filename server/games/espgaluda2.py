"""Nickname-based score service; Python 3.10+, SQLite, loopback behind HTTPS.

Score storage/ranking and bounded HTTP handling are adapted from
Mushihimesama-Futari-PC 7dced5979076329db71b1296c81944c83fb72b48,
server/futari_live.py. Game identity and native schemas remain separate.
"""
from __future__ import annotations

import argparse
import base64
import binascii
from contextlib import contextmanager
from dataclasses import dataclass
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


@dataclass(frozen=True)
class Game:
    name: str
    title: str
    views: range


G2 = Game('espgaluda2', '435A07D8', range(1, 85))
PROTOCOL = 1
MAX_SCORES = 100000
REPLAY_FORMAT = 'espgaluda2-native-v2'
REPLAY_HEADER = 104
MAX_REPLAY = 8 * 1024 * 1024
MAX_REPLAY_STORAGE = 512 * 1024 * 1024
LOG = logging.getLogger('cave-community')


class ApiError(Exception):
    def __init__(self, status, code):
        self.status, self.code = status, code


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ApiError(400, 'invalid_integer')
    return value


def text(value, limit):
    if not isinstance(value, str) or not value or len(value) > limit:
        raise ApiError(400, 'invalid_text')
    if any(unicodedata.category(c).startswith('C') for c in value):
        raise ApiError(400, 'invalid_text')
    return value


def nickname(value):
    result = unicodedata.normalize('NFKC', text(value, 32)).strip()
    if not result or len(result.encode('utf-8')) > 48 or len(result.encode('utf-16-le')) > 64:
        raise ApiError(400, 'invalid_nickname')
    return result


def decimal(value, low=0, high=(1 << 63) - 1):
    if not isinstance(value, str) or not re.fullmatch(r'-?[0-9]{1,20}', value):
        raise ApiError(400, 'decimal_string_required')
    return integer(int(value), low, high)


def columns(value):
    if not isinstance(value, list) or not 1 <= len(value) <= 64:
        raise ApiError(400, 'invalid_columns')
    result, ids = [], set()
    for item in value:
        if not isinstance(item, dict) or set(item) != {'id', 'type', 'value'}:
            raise ApiError(400, 'invalid_column')
        cid = integer(item['id'], 0, 0xffffffff)
        if cid in ids:
            raise ApiError(400, 'duplicate_column')
        ids.add(cid)
        typ = integer(item['type'], 1, 7)
        if typ == 1:
            val = integer(item['value'], -(1 << 31), (1 << 31) - 1)
        elif typ in (2, 7):
            val = str(decimal(item['value'], -(1 << 63)))
        else:
            raise ApiError(400, 'unsupported_column_type')
        result.append(dict(id=cid, type=typ, value=val))
    return sorted(result, key=lambda col: col['id'])


def reference(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{32}', value):
        raise ApiError(400, 'invalid_reference')
    return value


def replay_info(data):
    if not REPLAY_HEADER < len(data) <= MAX_REPLAY:
        raise ApiError(400, 'invalid_replay_size')
    local_checksum, version, timestamp, score, mode = struct.unpack_from('>IIQQI', data)
    character = struct.unpack_from('>I', data, 36)[0]
    payload = struct.unpack_from('>I', data, 96)[0]
    # Word 0 is filled by the title's later local-save path. A native network
    # upload has zero here; SHA-256 protects transport without rewriting it.
    if (version != 2 or payload + REPLAY_HEADER != len(data)
            or not 0 < timestamp < 1 << 63 or score >= 1 << 63 or character > 2):
        raise ApiError(400, 'invalid_replay_envelope')
    return timestamp, score, character


def match_replay(data, props):
    timestamp, score, character = replay_info(data)
    expected = {0x20000001: (2, score), 0x20000011: (2, timestamp),
                0x10000008: (1, character), 0x10008011: (1, len(data))}
    actual = {p['id']: (p['type'], int(p['value'])) for p in props}
    if any(actual.get(key) != value for key, value in expected.items()):
        raise ApiError(409, 'replay_score_mismatch')


class Store:
    def __init__(self, database, game=G2):
        self.database, self.game = str(database), game
        with self.connect() as db:
            db.executescript('''
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS identity(game TEXT, title TEXT, version INTEGER);
                CREATE TABLE IF NOT EXISTS scores(
                    id TEXT PRIMARY KEY, submission TEXT NOT NULL UNIQUE,
                    digest TEXT NOT NULL, nickname TEXT NOT NULL, view INTEGER NOT NULL,
                    rules TEXT NOT NULL, score INTEGER NOT NULL, columns_json TEXT NOT NULL,
                    build TEXT NOT NULL, created INTEGER NOT NULL);
                CREATE INDEX IF NOT EXISTS board_order ON scores(view,rules,score DESC);
                CREATE INDEX IF NOT EXISTS player_board ON scores(nickname,view,rules,score DESC);
                CREATE TABLE IF NOT EXISTS replays(
                    id TEXT PRIMARY KEY, sha256 TEXT NOT NULL UNIQUE,
                    data BLOB NOT NULL, created INTEGER NOT NULL);
                CREATE TABLE IF NOT EXISTS attachments(
                    score_id TEXT PRIMARY KEY REFERENCES scores(id),
                    replay_id TEXT NOT NULL REFERENCES replays(id));
            ''')
            identity = db.execute('SELECT game,title,version FROM identity').fetchall()
            if not identity:
                db.execute('INSERT INTO identity VALUES(?,?,1)', (game.name, game.title))
            elif identity != [(game.name, game.title, 1)]:
                raise RuntimeError('Community database belongs to a different game/schema')

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.database, timeout=5)
        db.execute('PRAGMA busy_timeout=5000')
        db.execute('PRAGMA foreign_keys=ON')
        try:
            with db:
                yield db
        finally:
            db.close()

    def board(self, view, rules):
        integer(view, 0, 0xffffffff)
        if view not in self.game.views or rules != 'standard':
            raise ApiError(400, 'unsupported_board')
        return view, rules

    def submit(self, body):
        if not isinstance(body, dict):
            raise ApiError(400, 'invalid_object')
        if body.get('title_id') != self.game.title or type(body.get('protocol')) is not int or body['protocol'] != PROTOCOL:
            raise ApiError(409, 'incompatible_client')
        sub = body.get('submission')
        if not isinstance(sub, str) or not re.fullmatch('[0-9a-f]{32}', sub):
            raise ApiError(400, 'invalid_submission')
        name = nickname(body.get('nickname'))
        view, rules = self.board(body.get('view'), body.get('rules'))
        score = decimal(body.get('score'))
        props = columns(body.get('columns'))
        total = next((p for p in props if p['id'] == 0x20000001 and p['type'] == 2), None)
        if total is None or int(total['value']) != score:
            raise ApiError(400, 'native_score_mismatch')
        build = text(body.get('build'), 96)
        canonical = json.dumps([name, view, rules, str(score), props, build],
                               ensure_ascii=False, sort_keys=True, separators=(',', ':'))
        digest = hashlib.sha256(canonical.encode()).hexdigest()
        sid = uuid.uuid4().hex
        with self.connect() as db:
            db.execute('BEGIN IMMEDIATE')
            old = db.execute('SELECT id,digest FROM scores WHERE submission=?', (sub,)).fetchone()
            if old:
                if old[1] != digest:
                    raise ApiError(409, 'submission_conflict')
                return dict(id=old[0], duplicate=True)
            if db.execute('SELECT COUNT(*) FROM scores').fetchone()[0] >= MAX_SCORES:
                raise ApiError(507, 'score_storage_full')
            db.execute('INSERT INTO scores VALUES(?,?,?,?,?,?,?,?,?,?)',
                       (sid, sub, digest, name, view, rules, score, json.dumps(props), build, int(time.time())))
        return dict(id=sid, duplicate=False)

    def boards(self):
        with self.connect() as db:
            rows = db.execute('SELECT view,rules,COUNT(DISTINCT nickname) FROM scores '
                              'GROUP BY view,rules ORDER BY view,rules').fetchall()
        return dict(boards=[dict(view=v, rules=r, players=n) for v, r, n in rows])

    def leaderboard(self, view, rules='standard', start=1, count=20, around=None, names=None):
        view, rules = self.board(view, rules)
        integer(start, 1, 1000000)
        integer(count, 1, 100)
        if around is not None:
            around = nickname(around)
        if names is not None:
            if not isinstance(names, list) or len(names) > 100:
                raise ApiError(400, 'too_many_names')
            names = [nickname(n) for n in names]
        sql = '''WITH personal AS(
            SELECT s.*,s.rowid AS sequence,
                   ROW_NUMBER() OVER(PARTITION BY nickname ORDER BY score DESC,s.rowid) AS p
            FROM scores s WHERE view=? AND rules=?), ranked AS(
            SELECT *,ROW_NUMBER() OVER(ORDER BY score DESC,sequence) AS rank
            FROM personal WHERE p=1)
            SELECT id,nickname,score,columns_json,build,created,rank FROM ranked'''
        with self.connect() as db:
            args = [view, rules]
            total = db.execute('SELECT COUNT(DISTINCT nickname) FROM scores WHERE view=? AND rules=?', args).fetchone()[0]
            if around:
                found = db.execute(sql + ' WHERE nickname=?', args + [around]).fetchone()
                if found:
                    start = max(1, found[6] - count // 2)
            if names is not None:
                rows = [] if not names else db.execute(sql + ' WHERE nickname IN (' + ','.join('?' for _ in names)
                                                       + ') ORDER BY rank', args + names).fetchall()
            else:
                rows = db.execute(sql + ' WHERE rank>=? ORDER BY rank LIMIT ?', args + [start, count]).fetchall()
            entries = []
            for row in rows:
                attachment = db.execute('SELECT r.id,r.sha256,length(r.data) FROM attachments a '
                                        'JOIN replays r ON r.id=a.replay_id WHERE a.score_id=?', (row[0],)).fetchone()
                entry = dict(id=row[0], nickname=row[1], score=str(row[2]), columns=json.loads(row[3]),
                             build=row[4], created=row[5], rank=row[6], replay_id=None)
                if attachment:
                    entry.update(replay_id=attachment[0], replay_sha256=attachment[1],
                                 replay_size=attachment[2], replay_format=REPLAY_FORMAT)
                entries.append(entry)
        return dict(view=view, rules=rules, total=total, start=start, rows=entries)

    def attach_replay(self, body):
        if not isinstance(body, dict):
            raise ApiError(400, 'invalid_object')
        if body.get('title_id') != self.game.title or type(body.get('protocol')) is not int or body['protocol'] != PROTOCOL:
            raise ApiError(409, 'incompatible_client')
        score_id, name = reference(body.get('score_id')), nickname(body.get('nickname'))
        if body.get('format') != REPLAY_FORMAT:
            raise ApiError(400, 'unsupported_replay_format')
        encoded = body.get('data')
        if not isinstance(encoded, str) or not 0 < len(encoded) <= (MAX_REPLAY + 2) // 3 * 4:
            raise ApiError(400, 'invalid_replay_encoding')
        try:
            data = base64.b64decode(encoded, validate=True)
        except (ValueError, binascii.Error):
            raise ApiError(400, 'invalid_replay_encoding') from None
        if base64.b64encode(data).decode('ascii') != encoded:
            raise ApiError(400, 'noncanonical_replay_encoding')
        replay_info(data)
        digest = hashlib.sha256(data).hexdigest()
        if body.get('sha256') != digest:
            raise ApiError(400, 'replay_hash_mismatch')
        with self.connect() as db:
            db.execute('BEGIN IMMEDIATE')
            score = db.execute('SELECT nickname,columns_json FROM scores WHERE id=?', (score_id,)).fetchone()
            if not score:
                raise ApiError(404, 'score_not_found')
            if score[0] != name:
                raise ApiError(409, 'score_owner_mismatch')
            match_replay(data, json.loads(score[1]))
            old = db.execute('SELECT r.id,r.sha256 FROM attachments a JOIN replays r '
                             'ON r.id=a.replay_id WHERE a.score_id=?', (score_id,)).fetchone()
            if old and old[1] != digest:
                raise ApiError(409, 'replay_attachment_conflict')
            existing = db.execute('SELECT id FROM replays WHERE sha256=?', (digest,)).fetchone()
            replay_id = existing[0] if existing else uuid.uuid4().hex
            if not existing:
                size = db.execute('SELECT COALESCE(SUM(length(data)),0) FROM replays').fetchone()[0]
                if size + len(data) > MAX_REPLAY_STORAGE:
                    raise ApiError(507, 'replay_storage_full')
                db.execute('INSERT INTO replays VALUES(?,?,?,?)', (replay_id, digest, data, int(time.time())))
            db.execute('INSERT OR IGNORE INTO attachments VALUES(?,?)', (score_id, replay_id))
        return dict(id=replay_id, score_id=score_id, sha256=digest, bytes=len(data), duplicate=bool(old))

    def replay(self, replay_id):
        with self.connect() as db:
            row = db.execute('SELECT sha256,data FROM replays WHERE id=?', (reference(replay_id),)).fetchone()
        if not row:
            raise ApiError(404, 'replay_not_found')
        if hashlib.sha256(row[1]).hexdigest() != row[0]:
            raise RuntimeError('Stored replay checksum mismatch')
        return dict(id=replay_id, format=REPLAY_FORMAT, sha256=row[0], bytes=len(row[1]),
                    data=base64.b64encode(row[1]).decode('ascii'))

    def estimate(self, view, rules, score, name):
        view, rules = self.board(view, rules)
        score, name = decimal(score), nickname(name)
        with self.connect() as db:
            rank = db.execute('SELECT COUNT(*)+1 FROM (SELECT nickname,MAX(score) AS best FROM scores '
                              'WHERE view=? AND rules=? AND nickname!=? GROUP BY nickname) WHERE best>=?',
                              (view, rules, name, score)).fetchone()[0]
        return dict(view=view, rules=rules, rank=rank)


class Server(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 32

    def __init__(self, address, store):
        self.store = store
        self.prefix = '/cave/api/v1/games/' + store.game.name
        self.slots = threading.BoundedSemaphore(4)
        super().__init__(address, Handler)

    def process_request(self, request, address):
        if not self.slots.acquire(False):
            request.close()
            return
        try:
            super().process_request(request, address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()


class Handler(BaseHTTPRequestHandler):
    server_version = 'CaveCommunity/1'
    sys_version = ''

    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def log_message(self, *_args):
        pass

    def reply(self, status, body):
        body.update(protocol=PROTOCOL, title_id=self.server.store.game.title)
        data = json.dumps(body, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Connection', 'close')
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.api(False)

    def do_POST(self):
        self.api(True)

    def api(self, post):
        request_id = uuid.uuid4().hex[:12]
        started = time.monotonic()
        route = 'unknown'
        try:
            try:
                if len(self.path) > 8192:
                    raise ApiError(414, 'uri_too_long')
                url = urlsplit(self.path)
                if not url.path.startswith(self.server.prefix + '/'):
                    raise ApiError(404, 'not_found')
                route = url.path[len(self.server.prefix):]
                query = parse_qs(url.query, max_num_fields=110, strict_parsing=True) if url.query else {}

                def one(key, default=None):
                    values = query.get(key, [default])
                    if len(values) != 1:
                        raise ApiError(400, 'duplicate_parameter')
                    return values[0]

                body = None
                if post:
                    if self.headers.get('Transfer-Encoding') or len(self.headers.get_all('Content-Length', [])) != 1:
                        raise ApiError(400, 'invalid_length')
                    if self.headers.get_content_type() != 'application/json':
                        raise ApiError(415, 'json_required')
                    length = int(self.headers.get('Content-Length', '-1'))
                    limit = 12 * 1024 * 1024 if route == '/replays' else 65536
                    if not 0 < length <= limit:
                        raise ApiError(413, 'body_too_large')
                    raw = self.rfile.read(length)
                    if len(raw) != length:
                        raise ApiError(400, 'incomplete_body')
                    body = json.loads(raw)
                store = self.server.store
                if not post and route == '/health':
                    result = dict(status='ok', features=['scores', 'leaderboards', 'estimate', 'replays'])
                elif not post and route == '/boards':
                    result = store.boards()
                elif not post and route == '/leaderboard':
                    result = store.leaderboard(int(one('view')), one('rules', 'standard'), int(one('start', '1')),
                                               int(one('count', '20')), one('around'), query.get('name'))
                elif not post and route == '/estimate':
                    result = store.estimate(int(one('view')), one('rules', 'standard'), one('score'), one('name'))
                elif post and route == '/scores':
                    result = store.submit(body)
                elif post and route == '/replays':
                    result = store.attach_replay(body)
                elif not post and re.fullmatch('/replays/[0-9a-f]{32}', route):
                    result = store.replay(route.rsplit('/', 1)[1])
                else:
                    raise ApiError(404, 'not_found')
                self.reply(200, result)
                LOG.info('request=%s route=%s status=200 ms=%d', request_id, route, (time.monotonic()-started)*1000)
            except ApiError as error:
                self.reply(error.status, dict(error=error.code, request=request_id))
            except (ValueError, TypeError, UnicodeError, RecursionError):
                self.reply(400, dict(error='invalid_request', request=request_id))
            except Exception:
                # No bodies, nicknames, IPs or full URLs in logs.
                LOG.error('request=%s internal_error', request_id)
                self.reply(500, dict(error='internal_error', request=request_id))
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, TimeoutError):
            LOG.info('request=%s disconnected', request_id)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', type=Path, required=True)
    parser.add_argument('--port', type=int, default=18086)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)s %(message)s')
    server = Server(('127.0.0.1', args.port), Store(args.database))

    def shutdown(_signal, _frame):
        threading.Thread(target=server.shutdown, daemon=True).start()

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)
    LOG.info('ready game=%s port=%d', server.store.game.name, server.server_port)
    try:
        server.serve_forever(poll_interval=0.25)
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
