"""Read-only report dashboard; expose through an authenticated HTTPS proxy."""
import argparse
from html import escape
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
from urllib.parse import parse_qs, quote, urlencode, urlsplit

if __package__:
    from . import file_uploads
else:
    import file_uploads

PREFIX = '/cave/reports/'
REPORT_ID = re.compile(r'\d{8}-[0-9a-f]{16}\Z')


def list_reports(directory, game='', query='', limit=100, offset=0, files=False, kind=''):
    result = []
    log_paths = () if kind == 'file' else Path(directory).glob('*.json')
    for path in log_paths:
        if not REPORT_ID.fullmatch(path.stem) or path.is_symlink():
            continue
        try:
            item = json.loads(path.read_text(encoding='utf-8'))
            if not isinstance(item, dict) or item.get('id') != path.stem:
                continue
            archive = path.with_suffix('.zip')
            if archive.is_symlink() or not archive.is_file():
                continue
            item = {key: item.get(key, '') for key in
                    ('id', 'received', 'game', 'version', 'nickname', 'message', 'run', 'bytes')}
            if not all(isinstance(item[k], str) for k in ('id', 'received', 'game', 'version', 'nickname', 'message', 'run')):
                continue
            if game and item['game'] != game:
                continue
            if query and query.casefold() not in ' '.join(str(v) for v in item.values()).casefold():
                continue
            item['download_url'] = PREFIX + 'download/' + quote(item['id']) + '.zip'
            result.append(item)
        except (OSError, ValueError, UnicodeError):
            continue
    if files and kind != 'log':
        for path in (Path(directory) / 'files').glob('*.json'):
            try:
                item = file_report(path)
                if game and item['game'] != game:
                    continue
                if query and query.casefold() not in ' '.join(str(v) for v in item.values()).casefold():
                    continue
                item['download_url'] = PREFIX + 'files/' + item['id']
                result.append(item)
            except (OSError, ValueError):
                continue
    result.sort(key=lambda item: (item['received'], item['id']), reverse=True)
    return {'total': len(result), 'offset': offset, 'limit': limit, 'reports': result[offset:offset + limit]}


def file_report(path):
    if not REPORT_ID.fullmatch(path.stem) or path.is_symlink():
        raise ValueError('Invalid file report path')
    item = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(item, dict) or item.get('id') != path.stem or item.get('kind') != 'file':
        raise ValueError('Invalid file report identity')
    if (type(item.get('bytes')) is not int or not 0 < item['bytes'] <= file_uploads.MAX_SIZE or
            not all(isinstance(item.get(key), str) for key in ('received', 'game', 'version', 'message', 'name'))):
        raise ValueError('Invalid file report metadata')
    return {key: item.get(key, '') for key in ('id', 'received', 'game', 'version', 'message', 'bytes', 'name', 'kind')}


def file_size(size):
    for unit, factor in (('GiB', 1024**3), ('MiB', 1024**2), ('KiB', 1024)):
        if size >= factor:
            return f'{size / factor:.2f} {unit}'
    return f'{size} bytes'


def html_page(data, game, query, files_only=False):
    title = 'Uploaded files' if files_only else 'Player reports'
    player_column = '' if files_only else '<th scope="col">Player</th>'
    file_column = 'File' if files_only else 'Download'
    current = ' aria-current="page"'
    tabs = (f'<a href="{PREFIX}"{current if not files_only else ""}>All reports</a>'
            f'<a href="{PREFIX}uploads/"{current if files_only else ""}>Uploaded files</a>')
    rows = []
    for item in data['reports']:
        value = lambda k: escape(str(item.get(k, '')), quote=True)
        download = value('name') if item.get('kind') == 'file' else 'ZIP'
        size = ''
        if item.get('kind') == 'file':
            size = f'<small title="{item["bytes"]:,} bytes">{file_size(item["bytes"])}</small>'
        player = '' if files_only else '<td>'+value('nickname')+'</td>'
        rows.append('<tr><td>'+value('received')+'</td><td>'+value('game')+'</td><td>'+value('version')+
                    '</td>'+player+'<td class="message">'+value('message')+
                    '</td><td class="file"><a href="'+value('download_url')+'">'+download+'</a>'+size+
                    '<small>'+value('id')+'</small></td></tr>')
    nav = ''
    for label, target in [('Previous', max(0, data['offset']-data['limit'])),
                          ('Next', data['offset']+data['limit'])]:
        if (label == 'Previous' and data['offset'] > 0) or (label == 'Next' and target < data['total']):
            page_query = urlencode(dict(game=game, q=query, limit=data['limit'], offset=target))
            nav += '<a href="?'+escape(page_query, quote=True)+'">'+label+'</a> '
    if not rows:
        message = 'No matching files.' if files_only else 'No matching reports.'
        rows.append(f'<tr><td colspan="{5 if files_only else 6}">{message}</td></tr>')
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EVAC · '''+title+'''</title><style>
body{font:15px system-ui,sans-serif;background:#f5f6f4;color:#222;margin:0}main{max-width:1400px;margin:36px auto;padding:0 24px}h1{font-size:25px}form{display:flex;flex-wrap:wrap;gap:8px;margin:20px 0}input,button{font:inherit;padding:9px 12px;border:1px solid #cbd2cd;border-radius:6px}button{background:#305c47;color:white;cursor:pointer}table{width:100%;border-collapse:collapse;background:white}th,td{text-align:left;padding:13px 12px;border-bottom:1px solid #e7eae7;vertical-align:top}th{font-size:12px;color:#65736b}.message{white-space:pre-wrap;overflow-wrap:anywhere;min-width:200px}.file{min-width:185px;max-width:380px;overflow-wrap:anywhere}small{display:block;color:#65736b;margin-top:5px}a{color:#236a4a}.wrap{overflow-x:auto}nav{display:flex;gap:14px;margin:20px 0}nav[aria-label="Report views"] a{padding:7px 0;text-decoration:none;border-bottom:2px solid transparent}nav a[aria-current=page]{color:#222;border-color:#305c47;font-weight:600}</style><main>
<h1>'''+title+'''</h1><nav aria-label="Report views">'''+tabs+'''</nav>
<form><input name="q" aria-label="Search" placeholder="'''+('Filename, message or report ID' if files_only else 'Search reports')+'''" value="'''+escape(query,quote=True)+'''">
<input name="game" aria-label="Game ID" placeholder="Game ID" value="'''+escape(game,quote=True)+'''"><button>Search</button></form>
<p>'''+str(data['total'])+(' files' if files_only else ' reports')+'''</p><div class="wrap"><table><thead><tr><th scope="col">Received (UTC)</th><th scope="col">Game</th><th scope="col">Version</th>'''+player_column+'''<th scope="col">Message</th><th scope="col">'''+file_column+'''</th></tr></thead><tbody>'''+''.join(rows)+'''</tbody></table></div>'''+('<nav aria-label="Pages">'+nav+'</nav>' if nav else '')+'''</main></html>'''
    return page.encode('utf-8')


class Handler(BaseHTTPRequestHandler):
    def respond(self, status, body, content_type, extra=()):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy', "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'")
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlsplit(self.path)
        params = parse_qs(url.query)
        files_only = url.path in (PREFIX + 'uploads', PREFIX + 'uploads/')
        api = url.path == PREFIX + 'api/reports'
        if url.path == PREFIX or files_only or api:
            try:
                limit = min(100, max(1, int(params.get('limit', ['50'])[0])))
                offset = max(0, int(params.get('offset', ['0'])[0]))
            except ValueError:
                self.respond(400, b'Invalid pagination', 'text/plain; charset=utf-8'); return
            game, query = params.get('game', [''])[0][:128], params.get('q', [''])[0][:256]
            kind = params.get('kind', [''])[0] if api else ''
            if files_only:
                kind = 'file'
            if kind not in ('', 'log', 'file'):
                self.respond(400, b'Invalid report kind', 'text/plain; charset=utf-8'); return
            data = list_reports(self.server.directory, game, query, limit, offset, self.server.storage is not None, kind)
            if api:
                self.respond(200, json.dumps(data, ensure_ascii=False).encode(), 'application/json; charset=utf-8')
            else:
                self.respond(200, html_page(data, game, query, files_only), 'text/html; charset=utf-8')
            return
        match = re.fullmatch(re.escape(PREFIX) + r'download/(\d{8}-[0-9a-f]{16})\.zip', url.path)
        if match:
            path = Path(self.server.directory) / (match.group(1) + '.zip')
            if not path.is_symlink() and path.is_file():
                self.respond(200, path.read_bytes(), 'application/zip',
                             [('Content-Disposition', 'attachment; filename="'+match.group(1)+'.zip"')])
                return
        identifier = url.path.removeprefix(PREFIX + 'files/')
        if self.server.storage is not None and REPORT_ID.fullmatch(identifier):
            self.download_file(identifier)
            return
        self.respond(404, b'Not found', 'text/plain; charset=utf-8')

    def download_file(self, identifier):
        try:
            item = file_report(Path(self.server.directory) / 'files' / (identifier + '.json'))
        except (OSError, ValueError):
            self.respond(404, b'Not found', 'text/plain; charset=utf-8')
            return
        try:
            source = self.server.storage.download(file_uploads.Uploads.key(identifier))
        except self.server.storage.oss_error:
            self.respond(503, b'File storage unavailable', 'text/plain; charset=utf-8')
            return
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(item['bytes']))
        self.send_header('Content-Disposition', "attachment; filename*=UTF-8''" + quote(item['name'], safe=''))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        try:
            for part in iter(lambda: source.read(1024**2), b''):
                self.wfile.write(part)
        finally:
            source.close()


def serve(directory, port=18094, storage=None):
    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    server.directory = Path(directory)
    server.storage = storage
    return server


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--port', type=int, default=18094)
    file_uploads.add_oss_arguments(parser)
    args = parser.parse_args()
    storage = file_uploads.oss_from_args(args) if args.oss_bucket else None
    serve(args.directory, args.port, storage).serve_forever()
