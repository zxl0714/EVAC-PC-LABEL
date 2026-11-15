"""One-file player reports backed by private OSS multipart objects."""

from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import threading
import time
import uuid


ROUTE = '/cave/api/v1/file-reports'
PART_SIZE = 16 * 1024**2
MAX_SIZE = 16 * 1024**3
ACTIVE_LIFETIME = 24 * 60 * 60
PART_PATH = re.compile(r'/([1-9]\d{0,3})\Z')
TOKEN = re.compile(r'Bearer ([0-9a-f]{32})\Z')


class UploadError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


class OssStorage:
    def __init__(self, bucket, region, endpoint, credential_directory):
        import oss2
        from oss2.models import PartInfo

        credentials = json.loads((Path(credential_directory) / 'oss-upload').read_text(encoding='utf-8'))
        self.bucket = oss2.Bucket(oss2.AuthV4(credentials['accessKeyID'], credentials['accessKeySecret']),
                                  endpoint, bucket, region=region, connect_timeout=30)
        self.part_info = PartInfo
        self.oss_error = oss2.exceptions.OssError
        self.no_upload = oss2.exceptions.NoSuchUpload

    def start(self, key):
        return self.bucket.init_multipart_upload(key, headers={'Content-Type': 'application/octet-stream'}).upload_id

    def put(self, key, upload, number, body):
        return self.bucket.upload_part(key, upload, number, body).etag

    def complete(self, key, upload, parts, size):
        try:
            self.bucket.complete_multipart_upload(key, upload, [self.part_info(i, etag) for i, etag in parts],
                                                  headers={'x-oss-object-acl': 'private'})
        except self.no_upload:
            pass  # OSS may have committed the object before a prior response was lost.
        if self.bucket.head_object(key).content_length != size or self.bucket.get_object_acl(key).acl != 'private':
            raise UploadError(503, 'Uploaded object could not be verified')

    def abort(self, key, upload):
        try:
            self.bucket.abort_multipart_upload(key, upload)
        except self.no_upload:
            pass

    def download(self, key):
        return self.bucket.get_object(key)


def oss_from_args(args):
    return OssStorage(args.oss_bucket, args.oss_region, args.oss_endpoint, os.environ['CREDENTIALS_DIRECTORY'])


def add_oss_arguments(parser):
    parser.add_argument('--oss-bucket')
    parser.add_argument('--oss-region', default='cn-beijing')
    parser.add_argument('--oss-endpoint', default='https://oss-cn-beijing.aliyuncs.com')


class Uploads:
    def __init__(self, directory, storage, games, part_size=PART_SIZE):
        self.directory = Path(directory) / 'files'
        self.pending = self.directory / 'pending'
        self.pending.mkdir(parents=True, exist_ok=True)
        self.storage = storage
        self.games = games
        self.part_size = part_size
        self.lock = threading.Lock()
        self.bodies = threading.BoundedSemaphore(4)

    @staticmethod
    def save(path, state):
        scratch = path.with_suffix('.tmp')
        scratch.write_text(json.dumps(state, ensure_ascii=False), encoding='utf-8')
        scratch.replace(path)

    @staticmethod
    def key(identifier):
        return 'player-file-reports/' + identifier

    def metadata(self, value):
        if not isinstance(value, dict) or value.get('game') not in self.games:
            raise UploadError(400, 'Unknown game')
        size = value.get('bytes')
        if type(size) is not int or not 0 < size <= MAX_SIZE:
            raise UploadError(413, 'File must be between 1 byte and 16 GiB')
        metadata = {'bytes': size, 'game': value['game']}
        for key, limit in (('name', 1024), ('version', 128), ('message', 8000)):
            text = value.get(key, '')
            if not isinstance(text, str) or len(text.encode('utf-8')) > limit:
                raise UploadError(400, 'Invalid ' + key)
            metadata[key] = text
        name = metadata['name']
        if not name or name in ('.', '..') or any(ord(c) < 32 or c in '/\\' for c in name):
            raise UploadError(400, 'Invalid file name')
        return metadata

    def prune(self):
        active = 0
        cutoff = time.time() - ACTIVE_LIFETIME
        for path in self.pending.glob('*.json'):
            state = json.loads(path.read_text(encoding='utf-8'))
            if path.stat().st_mtime < cutoff:
                if not state['complete']:
                    self.storage.abort(self.key(state['id']), state['upload'])
                path.unlink()
            elif not state['complete']:
                active += 1
        return active

    def begin(self, path, value):
        metadata = self.metadata(value)
        with self.lock:
            active = self.prune()
            if path.exists():
                state = json.loads(path.read_text(encoding='utf-8'))
                if state['metadata'] != metadata:
                    raise UploadError(409, 'Upload selection changed')
            else:
                if active >= 12:
                    raise UploadError(503, 'File uploads are busy')
                identifier = datetime.now(timezone.utc).strftime('%Y%m%d-') + uuid.uuid4().hex[:16]
                upload = self.storage.start(self.key(identifier))
                state = dict(id=identifier, upload=upload, metadata=metadata, parts={}, complete=False,
                             part_size=self.part_size)
                try:
                    self.save(path, state)
                except OSError:
                    self.storage.abort(self.key(identifier), upload)
                    raise
        # The report ID is returned only when the private object is complete.
        return {'part_size': state['part_size']}

    @staticmethod
    def load(path):
        if not path.is_file():
            raise UploadError(404, 'Upload session expired')
        return json.loads(path.read_text(encoding='utf-8'))

    @staticmethod
    def extent(state, number):
        size = state['metadata']['bytes'] - (number - 1) * state['part_size']
        if number < 1 or size <= 0:
            raise UploadError(400, 'Invalid part number')
        return min(size, state['part_size'])

    def put(self, path, number, body):
        digest = hashlib.sha256(body).hexdigest()
        with self.lock:
            state = self.load(path)
            if len(body) != self.extent(state, number):
                raise UploadError(400, 'Invalid part length')
            previous = state['parts'].get(str(number))
            if previous:
                if previous['sha256'] != digest:
                    raise UploadError(409, 'File changed during upload')
            else:
                etag = self.storage.put(self.key(state['id']), state['upload'], number, body)
                state['parts'][str(number)] = {'etag': etag, 'sha256': digest}
                self.save(path, state)
        return {'part': number}

    def complete(self, path):
        with self.lock:
            state = self.load(path)
            if not state['complete']:
                size = state['metadata']['bytes']
                count = (size + state['part_size'] - 1) // state['part_size']
                if len(state['parts']) != count:
                    raise UploadError(409, 'Upload is incomplete')
                parts = [(number, state['parts'][str(number)]['etag']) for number in range(1, count + 1)]
                self.storage.complete(self.key(state['id']), state['upload'], parts, size)
                record = dict(state['metadata'], id=state['id'], kind='file',
                              received=datetime.now(timezone.utc).isoformat())
                self.save(self.directory / (state['id'] + '.json'), record)
                state['complete'] = True
                self.save(path, state)
        return {'id': state['id']}

    def cancel(self, path):
        with self.lock:
            if path.exists():
                state = self.load(path)
                if not state['complete']:
                    self.storage.abort(self.key(state['id']), state['upload'])
                path.unlink()
        return {}

    @staticmethod
    def read(handler, limit, exact=False):
        length = int(handler.headers.get('Content-Length', '0'))
        if not 0 < length <= limit or (exact and length != limit):
            raise UploadError(413, 'Invalid request length')
        data = handler.rfile.read(length)
        if len(data) != length:
            raise UploadError(400, 'Incomplete request')
        return data

    def route(self, handler, method):
        suffix = handler.path[len(ROUTE):]
        match = TOKEN.fullmatch(handler.headers.get('Authorization', ''))
        if not match:
            raise UploadError(401, 'Missing upload session token')
        path = self.pending / (hashlib.sha256(match[1].encode('ascii')).hexdigest() + '.json')
        handler.connection.settimeout(60)
        if method == 'POST' and suffix == '':
            return self.begin(path, json.loads(self.read(handler, 16384)))
        if method == 'POST' and suffix == '/complete':
            return self.complete(path)
        if method == 'DELETE' and suffix == '':
            return self.cancel(path)
        part = PART_PATH.fullmatch(suffix)
        if method != 'PUT' or not part:
            raise UploadError(404, 'Unknown upload operation')
        number = int(part[1])
        if not self.bodies.acquire(blocking=False):
            raise UploadError(503, 'File uploads are busy')
        try:
            with self.lock:
                size = self.extent(self.load(path), number)
            return self.put(path, number, self.read(handler, size, exact=True))
        finally:
            self.bodies.release()
