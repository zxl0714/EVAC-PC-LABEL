"""Read-only report dashboard; expose through an authenticated HTTPS proxy."""
import argparse
from html import escape
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
from urllib.parse import parse_qs, quote, urlsplit

PREFIX = '/cave/reports/'
REPORT_ID = re.compile(r'\d{8}-[0-9a-f]{16}\Z')


def list_reports(directory, game='', query='', limit=100, offset=0):
    result = []
    for path in Path(directory).glob('*.json'):
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
    result.sort(key=lambda item: (item['received'], item['id']), reverse=True)
    return {'total': len(result), 'offset': offset, 'limit': limit, 'reports': result[offset:offset + limit]}


def html_page(data, game, query):
    rows = []
    for item in data['reports']:
        value = lambda k: escape(str(item[k]), quote=True)
        rows.append('<tr><td>'+value('received')+'</td><td>'+value('game')+'</td><td>'+value('version')+
                    '</td><td>'+value('nickname')+'</td><td class="message">'+value('message')+
                    '</td><td><a href="'+value('download_url')+'">ZIP</a><small>'+value('id')+'</small></td></tr>')
    nav = ''
    for label, target in [('Previous', max(0, data['offset']-data['limit'])),
                          ('Next', data['offset']+data['limit'])]:
        if (label == 'Previous' and data['offset'] > 0) or (label == 'Next' and target < data['total']):
            nav += '<a href="?game='+quote(game)+'&amp;q='+quote(query)+'&amp;offset='+str(target)+'">'+label+'</a> '
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EVAC · Player reports</title><style>
body{font:15px system-ui,sans-serif;background:#f5f6f4;color:#222;margin:0}main{max-width:1500px;margin:40px auto;padding:0 24px}h1{font-size:25px}form{display:flex;gap:8px;margin:24px 0}input,button{font:inherit;padding:9px 12px;border:1px solid #cbd2cd;border-radius:6px}button{background:#305c47;color:white;cursor:pointer}table{width:100%;border-collapse:collapse;background:white}th,td{text-align:left;padding:13px 12px;border-bottom:1px solid #e7eae7;vertical-align:top}th{font-size:12px;color:#65736b}.message{white-space:pre-wrap;overflow-wrap:anywhere;min-width:240px}small{display:block;color:#778078;margin-top:5px}a{color:#236a4a}.wrap{overflow-x:auto}nav{margin:20px 0}</style><main>
<h1>Player reports</h1><p>Recent reports and uploaded logs</p>
<form><input name="q" placeholder="Search messages or players" value="'''+escape(query,quote=True)+'''">
<input name="game" placeholder="Game ID" value="'''+escape(game,quote=True)+'''"><button>Search</button></form>
<p>'''+str(data['total'])+''' reports</p><div class="wrap"><table><thead><tr><th>Received (UTC)</th><th>Game</th><th>Version</th><th>Player</th><th>Message</th><th>Log</th></tr></thead><tbody>'''+''.join(rows)+'''</tbody></table></div><nav>'''+nav+'''</nav></main></html>'''
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
        if url.path in (PREFIX, PREFIX + 'api/reports'):
            try:
                limit = min(100, max(1, int(params.get('limit', ['50'])[0])))
                offset = max(0, int(params.get('offset', ['0'])[0]))
            except ValueError:
                self.respond(400, b'Invalid pagination', 'text/plain; charset=utf-8'); return
            game, query = params.get('game', [''])[0][:128], params.get('q', [''])[0][:256]
            data = list_reports(self.server.directory, game, query, limit, offset)
            if url.path.endswith('/api/reports'):
                self.respond(200, json.dumps(data, ensure_ascii=False).encode(), 'application/json; charset=utf-8')
            else:
                self.respond(200, html_page(data, game, query), 'text/html; charset=utf-8')
            return
        match = re.fullmatch(re.escape(PREFIX) + r'download/(\d{8}-[0-9a-f]{16})\.zip', url.path)
        if match:
            path = Path(self.server.directory) / (match.group(1) + '.zip')
            if not path.is_symlink() and path.is_file():
                self.respond(200, path.read_bytes(), 'application/zip',
                             [('Content-Disposition', 'attachment; filename="'+match.group(1)+'.zip"')])
                return
        self.respond(404, b'Not found', 'text/plain; charset=utf-8')


def serve(directory, port=18094):
    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    server.directory = Path(directory)
    return server


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--port', type=int, default=18094)
    args = parser.parse_args()
    serve(args.directory, args.port).serve_forever()
