"""Ketsui community score/replay service, Python 3.10+ and SQLite.

The native NpScore bridge is the intended client. Scores precede replay uploads;
immutable receipts bind those uploads to one exact submission. Run on loopback
behind an HTTPS reverse proxy. Importing this module starts no service.
"""
from __future__ import annotations

import argparse
import base64
import binascii
from contextlib import closing, contextmanager
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
from pathlib import Path
import re
import signal
import sqlite3
import threading
import time
import unicodedata
from urllib.parse import parse_qs, urlsplit
import uuid

API = '/ketsui/api/v1'
TITLE = 'CUSA09062'
VERSION = '1.02'
PROTOCOL = 1
REPLAY_FORMAT = 'ketsui-npscore-1.02'
MAX_REPLAY = 8 * 1024 * 1024
MAX_BODY = ((MAX_REPLAY + 2) // 3) * 4 + 4096
MAX_SCORES = 100000
MAX_REPLAY_STORAGE = 512 * 1024 * 1024
SCHEMA = 1
APPLICATION_ID = 0x4B545356
LOG = logging.getLogger('ketsui-live')

# Human-maintained mapping from the verified title profile, not exported assets.
BOARD_GROUPS = (
    (0, 'arcade'), (10, 'arcade_2'), (20, 'arcade_sp'),
    (30, 'super_easy'), (40, 'deathtiny'), (50, 'ikd_2007'), (60, 'custom'),
)
BOARDS = {base + ship: dict(id=base + ship, mode=mode,
                          ship=('all', 'type_a', 'type_b')[ship],
                          upload_limit=100 if ship == 0 else 10)
          for base, mode in BOARD_GROUPS for ship in range(3)}
BOARDS[99] = dict(id=99, mode='unclassified', ship='unclassified', upload_limit=0)


class ApiError(Exception):
    def __init__(self, status, code):
        super().__init__(code)
        self.status, self.code = status, code


def require(condition, code='invalid_request', status=400):
    if not condition:
        raise ApiError(status, code)


def integer(value, low, high):
    require(type(value) is int and low <= value <= high, 'invalid_integer')
    return value


def identifier(value):
    require(isinstance(value, str) and re.fullmatch('[0-9a-f]{32}', value), 'invalid_id')
    return value


def nickname(value):
    require(isinstance(value, str) and len(value) <= 32, 'invalid_nickname')
    require(not any(unicodedata.category(c).startswith('C') for c in value), 'invalid_nickname')
    value = unicodedata.normalize('NFKC', value).strip()
    require(bool(value) and len(value) <= 32 and len(value.encode('utf-8')) <= 48, 'invalid_nickname')
    require(not any(unicodedata.category(c).startswith('C') for c in value), 'invalid_nickname')
    return value


def account_id(name):
    # Internal native-field mapping only: not a login, password, or profile path.
    raw = hashlib.sha256(b'ketsui-community-v1\0' + name.encode('utf-8')).digest()
    return str((int.from_bytes(raw[:8], 'little') & ((1 << 63) - 1)) or 1)


def identity(value):
    fields(value, ('nickname',))
    name = nickname(value['nickname'])
    # Canonicalization only: no account registration or database row is needed.
    return dict(nickname=name, account_id=account_id(name))


def board_id(value):
    value = integer(value, 0, 0xffffffff)
    require(value in BOARDS, 'unknown_board')
    return value


def ruleset(value, board):
    require(isinstance(value, str) and re.fullmatch(r'[a-z0-9][a-z0-9._-]{0,79}', value), 'invalid_ruleset')
    # A CUSTOM configuration cannot silently share the unmodified-mode board.
    if board in (60, 61, 62):
        require(re.fullmatch(r'custom-[0-9a-f]{64}', value), 'custom_rules_required')
    return value


def decimal(value, low=0, high=(1 << 63) - 1):
    require(isinstance(value, str) and re.fullmatch(r'0|[1-9][0-9]{0,18}', value), 'invalid_decimal')
    result = int(value)
    require(low <= result <= high, 'invalid_decimal')
    return result


def binary(value, maximum, *, exact=None):
    require(isinstance(value, str) and len(value) <= ((maximum + 2) // 3) * 4, 'invalid_binary')
    try:
        result = base64.b64decode(value, validate=True)
    except (ValueError, binascii.Error):
        raise ApiError(400, 'invalid_binary') from None
    require(len(result) <= maximum and (exact is None or len(result) == exact), 'invalid_binary')
    require(base64.b64encode(result).decode('ascii') == value, 'noncanonical_binary')
    return result


def fields(value, names):
    require(type(value) is dict and set(value) == set(names), 'invalid_fields')


def strict_json(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, 'duplicate_field')
            result[key] = value
        return result

    def invalid_constant(_):
        raise ApiError(400, 'invalid_json')

    try:
        return json.loads(raw.decode('utf-8'), object_pairs_hook=pairs, parse_constant=invalid_constant)
    except (ValueError, UnicodeError, RecursionError):
        raise ApiError(400, 'invalid_json') from None


RANKED = '''WITH best AS (
    SELECT *,ROW_NUMBER() OVER(PARTITION BY nickname ORDER BY score DESC,sequence) AS choice
    FROM scores WHERE board=? AND rules=?
  ), ranked AS (
    SELECT *,ROW_NUMBER() OVER(ORDER BY score DESC,sequence) AS serial_rank,
      RANK() OVER(ORDER BY score DESC) AS rank
    FROM best WHERE choice=1
  ) SELECT r.*,p.sha256 AS replay_sha256,p.size AS replay_size,p.format AS replay_format
    FROM ranked r LEFT JOIN replays p ON p.entry_id=r.id'''


def check_database_identity(db, title=TITLE, application_id=APPLICATION_ID):
    tables = {row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'")}
    app_id = db.execute('PRAGMA application_id').fetchone()[0]
    if tables:
        if 'ketsui_meta' not in tables or app_id != application_id:
            raise RuntimeError('Database belongs to another application')
        if db.execute('SELECT title,version FROM ketsui_meta').fetchall() != [(title, SCHEMA)]:
            raise RuntimeError('Unsupported PS4 community database schema')
    elif app_id not in (0, application_id):
        raise RuntimeError('Database belongs to another application')


class Store:
    # Legacy names and database schema remain stable for existing Ketsui users.
    # A second title supplies only its protocol/validation profile, never a
    # process-global replacement of another running service's constants.
    title, version, api = TITLE, VERSION, API
    boards, replay_format = BOARDS, REPLAY_FORMAT
    application_id = APPLICATION_ID
    ruleset = staticmethod(ruleset)
    max_score = (1 << 63) - 1
    features = ('scores', 'leaderboards', 'replays')

    @staticmethod
    def account_id(name):
        return account_id(name)

    @classmethod
    def identity(cls, value):
        fields(value, ('nickname',))
        name = nickname(value['nickname'])
        return dict(nickname=name, account_id=cls.account_id(name))

    @classmethod
    def board_id(cls, value):
        value = integer(value, 0, 0xffffffff)
        require(value in cls.boards, 'unknown_board')
        return value

    def validate_submission(self, board, score):
        pass

    def validate_replay(self, payload, row):
        pass

    def __init__(self, database, *, max_scores=MAX_SCORES, max_replay_storage=MAX_REPLAY_STORAGE):
        self.database = str(Path(database).resolve())
        self.max_scores = integer(max_scores, 1, MAX_SCORES)
        self.max_replay_storage = integer(max_replay_storage, 1, MAX_REPLAY_STORAGE)
        Path(self.database).parent.mkdir(parents=True, exist_ok=True)
        # Inspect identity before changing journal mode/schema. In particular,
        # never migrate or add our tables to another game's existing database.
        if Path(self.database).exists():
            with closing(sqlite3.connect(Path(self.database).as_uri() + '?mode=ro', uri=True,
                                         timeout=5, isolation_level=None)) as inspection:
                check_database_identity(inspection, self.title, self.application_id)
        db = sqlite3.connect(self.database, timeout=5, isolation_level=None)
        try:
            check_database_identity(db, self.title, self.application_id)
            db.execute('PRAGMA journal_mode=WAL')
            db.execute('PRAGMA synchronous=FULL')
            db.executescript(f'''BEGIN IMMEDIATE;
              PRAGMA application_id={self.application_id};
              CREATE TABLE IF NOT EXISTS ketsui_meta(title TEXT NOT NULL,version INTEGER NOT NULL);
              INSERT INTO ketsui_meta SELECT '{self.title}',{SCHEMA} WHERE NOT EXISTS(SELECT 1 FROM ketsui_meta);
              CREATE TABLE IF NOT EXISTS players(nickname TEXT PRIMARY KEY,account_id TEXT NOT NULL UNIQUE);
              CREATE TABLE IF NOT EXISTS scores(
                sequence INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT NOT NULL UNIQUE,
                submission TEXT NOT NULL UNIQUE,digest TEXT NOT NULL,nickname TEXT NOT NULL REFERENCES players(nickname),
                board INTEGER NOT NULL,rules TEXT NOT NULL,score INTEGER NOT NULL,
                comment BLOB NOT NULL,game_info BLOB NOT NULL,build TEXT NOT NULL,
                created_us INTEGER NOT NULL,accepted INTEGER NOT NULL,receipt_rank INTEGER NOT NULL,
                upload_eligible INTEGER NOT NULL);
              CREATE INDEX IF NOT EXISTS ketsui_board ON scores(board,rules,nickname,score DESC,sequence);
              CREATE TABLE IF NOT EXISTS replays(
                entry_id TEXT PRIMARY KEY REFERENCES scores(id),sha256 TEXT NOT NULL,
                size INTEGER NOT NULL,format TEXT NOT NULL,payload BLOB NOT NULL);
              COMMIT;''')
        finally:
            db.close()

    @contextmanager
    def connect(self, *, write=False):
        db = sqlite3.connect(self.database, timeout=5, isolation_level=None)
        db.row_factory = sqlite3.Row
        try:
            db.execute('PRAGMA foreign_keys=ON')
            db.execute('PRAGMA synchronous=FULL')
            db.execute('BEGIN IMMEDIATE' if write else 'BEGIN')
            yield db
            db.commit()
        except BaseException:
            db.rollback()
            raise
        finally:
            db.close()

    @classmethod
    def receipt(cls, row):
        return dict(entry_id=row['id'], accepted=bool(row['accepted']),
                    serial_rank=row['receipt_rank'], upload_eligible=bool(row['upload_eligible']),
                    account_id=cls.account_id(row['nickname']))

    def submit(self, value):
        fields(value, ('submission_id', 'nickname', 'board', 'rules', 'score',
                       'comment_base64', 'game_info_base64', 'build'))
        submission = identifier(value['submission_id'])
        name = nickname(value['nickname'])
        board = self.board_id(value['board'])
        rules = self.ruleset(value['rules'], board)
        score = decimal(value['score'], high=self.max_score)
        self.validate_submission(board, score)
        comment = binary(value['comment_base64'], 64, exact=64)
        info = binary(value['game_info_base64'], 189)
        require(isinstance(value['build'], str) and re.fullmatch(r'[A-Za-z0-9._+-]{1,80}', value['build']), 'invalid_build')
        canonical = dict(value, nickname=name)
        digest = hashlib.sha256(json.dumps(canonical, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode('utf-8')).hexdigest()
        with self.connect(write=True) as db:
            previous = db.execute('SELECT * FROM scores WHERE submission=?', (submission,)).fetchone()
            if previous:
                require(previous['digest'] == digest, 'submission_conflict', 409)
                return self.receipt(previous)
            require(db.execute('SELECT COUNT(*) FROM scores').fetchone()[0] < self.max_scores, 'score_capacity', 507)
            account = self.account_id(name)
            collision = db.execute('SELECT nickname FROM players WHERE account_id=?', (account,)).fetchone()
            require(collision is None or collision[0] == name, 'account_mapping_collision', 409)
            db.execute('INSERT OR IGNORE INTO players VALUES(?,?)', (name, account))
            best = db.execute('SELECT MAX(score) FROM scores WHERE nickname=? AND board=? AND rules=?', (name, board, rules)).fetchone()[0]
            accepted = best is None or score > best
            entry = uuid.uuid4().hex
            db.execute('''INSERT INTO scores(id,submission,digest,nickname,board,rules,score,comment,game_info,
                       build,created_us,accepted,receipt_rank,upload_eligible) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,0,0)''',
                       (entry, submission, digest, name, board, rules, score, comment, info,
                        value['build'], time.time_ns() // 1000, int(accepted)))
            ranked = db.execute(RANKED + ' WHERE r.nickname=?', (board, rules, name)).fetchone()
            eligible = accepted and ranked['serial_rank'] <= self.boards[board]['upload_limit']
            db.execute('UPDATE scores SET receipt_rank=?,upload_eligible=? WHERE id=?',
                       (ranked['serial_rank'], int(eligible), entry))
            return self.receipt(db.execute('SELECT * FROM scores WHERE id=?', (entry,)).fetchone())

    @classmethod
    def entry(cls, row):
        return dict(entry_id=row['id'], nickname=row['nickname'], account_id=cls.account_id(row['nickname']),
                    score=str(row['score']), serial_rank=row['serial_rank'], rank=row['rank'],
                    created_us=str(row['created_us']), comment_base64=base64.b64encode(row['comment']).decode('ascii'),
                    game_info_base64=base64.b64encode(row['game_info']).decode('ascii'),
                    build=row['build'], replay=(None if row['replay_sha256'] is None else dict(
                        sha256=row['replay_sha256'], size=row['replay_size'], format=row['replay_format'])))

    def leaderboard(self, board, rules, start=1, count=20, account=None):
        board = self.board_id(board)
        rules = self.ruleset(rules, board)
        start, count = integer(start, 1, 0xffffffff), integer(count, 1, 100)
        if account is not None:
            decimal(account, 1)
        with self.connect() as db:
            total = db.execute('SELECT COUNT(DISTINCT nickname) FROM scores WHERE board=? AND rules=?', (board, rules)).fetchone()[0]
            if account is None:
                rows = db.execute(RANKED + ' WHERE r.serial_rank>=? ORDER BY r.serial_rank LIMIT ?',
                                  (board, rules, start, count)).fetchall()
            else:
                person = db.execute('SELECT nickname FROM players WHERE account_id=?', (account,)).fetchone()
                rows = [] if person is None else db.execute(RANKED + ' WHERE r.nickname=?', (board, rules, person[0])).fetchall()
            sorted_us = db.execute('SELECT COALESCE(MAX(created_us),0) FROM scores WHERE board=? AND rules=? AND accepted=1',
                                   (board, rules)).fetchone()[0]
            return dict(board=board, rules=rules, total=total, start=start,
                        sorted_us=str(sorted_us), entries=[self.entry(row) for row in rows])

    def upload(self, value):
        fields(value, ('entry_id', 'submission_id', 'nickname', 'format', 'sha256', 'payload_base64'))
        entry, submission = identifier(value['entry_id']), identifier(value['submission_id'])
        name = nickname(value['nickname'])
        require(value['format'] == self.replay_format, 'incompatible_replay', 409)
        require(isinstance(value['sha256'], str) and re.fullmatch('[0-9a-f]{64}', value['sha256']), 'invalid_sha256')
        payload = binary(value['payload_base64'], MAX_REPLAY)
        require(bool(payload) and hashlib.sha256(payload).hexdigest() == value['sha256'], 'replay_digest_mismatch')
        with self.connect(write=True) as db:
            row = db.execute('SELECT * FROM scores WHERE id=?', (entry,)).fetchone()
            require(row is not None, 'unknown_entry', 404)
            require(row['submission'] == submission and row['nickname'] == name, 'submission_mismatch', 409)
            require(row['accepted'] and row['upload_eligible'], 'replay_not_eligible', 409)
            self.validate_replay(payload, row)
            old = db.execute('SELECT sha256,size,format FROM replays WHERE entry_id=?', (entry,)).fetchone()
            if old:
                require((old['sha256'], old['size'], old['format']) == (value['sha256'], len(payload), value['format']), 'replay_conflict', 409)
            else:
                used = db.execute('SELECT COALESCE(SUM(size),0) FROM replays').fetchone()[0]
                require(used + len(payload) <= self.max_replay_storage, 'replay_capacity', 507)
                db.execute('INSERT INTO replays VALUES(?,?,?,?,?)', (entry, value['sha256'], len(payload), value['format'], payload))
            return dict(entry_id=entry, sha256=value['sha256'], size=len(payload), format=value['format'])

    def download(self, entry):
        entry = identifier(entry)
        with self.connect() as db:
            row = db.execute('SELECT * FROM replays WHERE entry_id=?', (entry,)).fetchone()
            require(row is not None, 'replay_not_found', 404)
            payload = bytes(row['payload'])
            require(len(payload) == row['size'] and hashlib.sha256(payload).hexdigest() == row['sha256'], 'stored_replay_corrupt', 500)
            return payload, row['sha256'], row['format']


class Handler(BaseHTTPRequestHandler):
    server_version = 'KetsuiCommunity/1'
    sys_version = ''

    def setup(self):
        self.request.settimeout(15)
        super().setup()

    def log_message(self, *_):
        pass  # Do not log raw URLs, names, addresses, payloads or headers.

    def reply(self, status, value, *, binary_headers=None):
        body = value if isinstance(value, bytes) else json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/octet-stream' if binary_headers else 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Connection', 'close')
        for name, value in (binary_headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def read_json(self):
        require(not self.headers.get_all('Transfer-Encoding'), 'unsupported_transfer_encoding')
        lengths = self.headers.get_all('Content-Length', [])
        require(len(lengths) == 1 and re.fullmatch(r'0|[1-9][0-9]{0,8}', lengths[0]), 'invalid_content_length')
        size = int(lengths[0])
        require(0 < size <= MAX_BODY, 'body_too_large', 413)
        types = self.headers.get_all('Content-Type', [])
        require(len(types) == 1 and types[0].lower() in ('application/json', 'application/json; charset=utf-8'), 'json_required', 415)
        raw = self.rfile.read(size)
        require(len(raw) == size, 'truncated_body')
        return strict_json(raw)

    def _dispatch(self):
        request = uuid.uuid4().hex[:12]
        route = 'unmatched'
        try:
            require(self.headers.get('Origin') is None, 'browser_origin_not_allowed', 403)
            require(len(self.path) <= 2048, 'uri_too_long', 414)
            parts = urlsplit(self.path)
            require(not parts.scheme and not parts.netloc and not parts.fragment, 'invalid_target')
            profile = self.server.store
            require(parts.path.startswith(profile.api + '/'), 'not_found', 404)
            route = parts.path[len(profile.api):]
            # Python 3.10 strict parsing rejects even the empty query string.
            # No query is a normal request; nonempty queries stay strict.
            query = parse_qs(parts.query, keep_blank_values=True, strict_parsing=True, max_num_fields=5) if parts.query else {}
            require(all(len(values) == 1 for values in query.values()), 'duplicate_query')
            if self.command == 'GET' and route == '/health':
                require(not query)
                self.reply(200, dict(status='ok', protocol=PROTOCOL, title_id=profile.title, game_version=profile.version,
                                    features=list(profile.features), max_replay_bytes=MAX_REPLAY))
            elif self.command == 'GET' and route == '/boards':
                require(not query)
                self.reply(200, dict(boards=list(profile.boards.values())))
            elif self.command == 'GET' and route == '/leaderboards':
                require(set(query) in ({'board', 'rules'}, {'board', 'rules', 'start', 'count'}, {'board', 'rules', 'account_id'}), 'invalid_query')
                def number(key, default, low, high):
                    text = query.get(key, [str(default)])[0]
                    require(re.fullmatch(r'0|[1-9][0-9]{0,9}', text), 'invalid_query_integer')
                    return integer(int(text), low, high)
                self.reply(200, self.server.store.leaderboard(
                    number('board', 0, 0, 0xffffffff), query['rules'][0],
                    number('start', 1, 1, 0xffffffff), number('count', 20, 1, 100),
                    query.get('account_id', [None])[0]))
            elif self.command == 'POST' and route in ('/identity', '/scores', '/replays'):
                require(not query)
                value = self.read_json()
                operation = profile.identity if route == '/identity' else profile.submit if route == '/scores' else profile.upload
                self.reply(200, operation(value))
            elif self.command == 'GET' and re.fullmatch(r'/entries/[0-9a-f]{32}/replay', route):
                require(not query)
                data, sha, fmt = self.server.store.download(route.split('/')[2])
                self.reply(200, data, binary_headers={'X-Replay-SHA256': sha, 'X-Replay-Format': fmt})
            else:
                raise ApiError(404, 'not_found')
            LOG.info('request=%s method=%s status=200', request, self.command)
        except ApiError as error:
            self.reply(error.status, dict(error=error.code, request=request))
            LOG.info('request=%s status=%d error=%s', request, error.status, error.code)
        except (ValueError, UnicodeError, RecursionError):
            self.reply(400, dict(error='invalid_request', request=request))
        except (BrokenPipeError, ConnectionError, TimeoutError):
            LOG.info('request=%s disconnected', request)
        except sqlite3.OperationalError:
            self.reply(503, dict(error='storage_unavailable', request=request))
            LOG.error('request=%s storage_unavailable', request)
        except Exception:
            # No request data or database paths in the public response/log.
            self.reply(500, dict(error='internal_error', request=request))
            LOG.error('request=%s internal_error', request)

    def dispatch(self):
        try:
            self._dispatch()
        except (BrokenPipeError, ConnectionError, TimeoutError):
            # The peer can also disconnect while an error response is sent.
            LOG.info('connection_closed_before_response')

    do_GET = dispatch
    do_POST = dispatch


class Server(ThreadingHTTPServer):
    daemon_threads = False
    request_queue_size = 32
    allow_reuse_address = True

    def __init__(self, port, store, *, max_connections=16):
        self.store = store
        self.slots = threading.BoundedSemaphore(integer(max_connections, 1, 64))
        super().__init__(('127.0.0.1', integer(port, 0, 65535)), Handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            try:
                request.settimeout(1)
                request.sendall(b'HTTP/1.0 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
            except OSError:
                pass
            finally:
                self.shutdown_request(request)
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

    def handle_error(self, *_):
        LOG.error('connection_handler_failed')


def serve(store_class=Store, default_port=8732):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', type=Path, required=True)
    parser.add_argument('--port', type=int, default=default_port)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)s %(message)s')
    server = Server(args.port, store_class(args.database))
    stopping = threading.Event()
    def stop(*_):
        if not stopping.is_set():
            stopping.set()
            threading.Thread(target=server.shutdown, daemon=True).start()
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    LOG.info('listening=127.0.0.1:%d protocol=%d title=%s', server.server_port, PROTOCOL, server.store.title)
    try:
        server.serve_forever(poll_interval=0.1)
    finally:
        server.server_close()


def main():
    serve()


if __name__ == '__main__':
    main()
