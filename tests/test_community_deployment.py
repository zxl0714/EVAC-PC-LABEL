"""Keep per-title service state and shared proxy routes independent."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
DEPLOY = ROOT / 'server/deploy'


class CommunityDeploymentTests(unittest.TestCase):
    def test_sdoj_uses_separate_release_pointer_port_and_writable_state(self):
        service = (DEPLOY / 'cave-sdoj.service.in').read_text(encoding='utf-8')
        for line in ('User=cave-community', 'Group=cave-community',
                     'WorkingDirectory=/opt/cave-community/sdoj-current',
                     'StateDirectory=cave-community/sdoj',
                     'ReadWritePaths=/var/lib/cave-community/sdoj',
                     'ProtectSystem=strict', 'NoNewPrivileges=true',
                     'ExecStart=/usr/bin/python3 -B -m server.games.sdoj --database '
                     '/var/lib/cave-community/sdoj/community.sqlite3 --port 18087'):
            self.assertIn(line, service.splitlines())
        self.assertNotIn('/opt/cave-community/current', service)
        self.assertNotIn('espgaluda2', service)

    def test_six_routes_keep_existing_upstreams_and_unknown_game_fallback(self):
        source = (DEPLOY / 'cave-live-locations.conf.in').read_text(encoding='utf-8')
        locations = dict(re.findall(r'^location ([^ ]+) \{\n(.*?)^\}', source, re.M | re.S))
        expected = {'espgaluda2': '127.0.0.1:18086', 'futari': '127.0.0.1:18084/futari/api/v1/',
                    'ketsui': '127.0.0.1:18085/ketsui/api/v1/', 'sdoj': '127.0.0.1:18087',
                    'daioujou': '127.0.0.1:18088', 'esprade': '127.0.0.1:18089'}
        self.assertEqual(set(locations), {f'/cave/api/v1/games/{game}/' for game in expected})
        for game, upstream in expected.items():
            block = locations[f'/cave/api/v1/games/{game}/']
            self.assertIn(f'proxy_pass http://{upstream};', block)
            self.assertIn('client_max_body_size 12m;', block)
            self.assertIn('limit_req_status 429;', block)
        self.assertIn('location /cave/api/v1/ { return 404; }', source.splitlines())

    def test_sdoj_backup_has_own_database_and_daily_timer(self):
        source = (DEPLOY / 'cave-sdoj-backup.service.in').read_text(encoding='utf-8')
        self.assertIn('After=cave-sdoj.service', source)
        self.assertIn('/opt/cave-community/sdoj-current/server/backup.py', source)
        self.assertIn('--database /var/lib/cave-community/sdoj/community.sqlite3', source)
        self.assertIn('--directory /var/lib/cave-community/sdoj/backups', source)
        self.assertNotIn('espgaluda2', source)
        timer = (DEPLOY / 'cave-sdoj-backup.timer.in').read_text(encoding='utf-8')
        self.assertIn('OnCalendar=daily', timer)
        self.assertIn('Persistent=true', timer)

    def test_esprade_has_separate_release_state_and_daily_backup(self):
        service = (DEPLOY / 'cave-esprade.service.in').read_text(encoding='utf-8')
        for line in ('User=cave-community', 'Group=cave-community',
                     'WorkingDirectory=/opt/cave-community/esprade-current',
                     'StateDirectory=cave-community/esprade',
                     'ReadWritePaths=/var/lib/cave-community/esprade',
                     'ProtectSystem=strict', 'NoNewPrivileges=true',
                     'ExecStart=/usr/bin/python3 -B -m server.games.esprade --database '
                     '/var/lib/cave-community/esprade/community.sqlite3 --port 18089'):
            self.assertIn(line, service.splitlines())
        backup = (DEPLOY / 'cave-esprade-backup.service.in').read_text(encoding='utf-8')
        for token in ('After=cave-esprade.service', '/opt/cave-community/esprade-current/server/backup.py',
                      '--database /var/lib/cave-community/esprade/community.sqlite3',
                      '--directory /var/lib/cave-community/esprade/backups'):
            self.assertIn(token, backup)
        for peer in ('futari', 'espgaluda2', 'ketsui', 'sdoj', 'daioujou', '/opt/cave-community/current'):
            self.assertNotIn(peer, service + backup)
        timer = (DEPLOY / 'cave-esprade-backup.timer.in').read_text(encoding='utf-8')
        self.assertIn('OnCalendar=daily', timer)
        self.assertIn('Persistent=true', timer)

    def test_daioujou_has_separate_release_state_and_backup_without_changing_peers(self):
        service = (DEPLOY / 'cave-daioujou.service.in').read_text(encoding='utf-8')
        for line in ('User=cave-community', 'Group=cave-community',
                     'WorkingDirectory=/opt/cave-community/daioujou-current',
                     'StateDirectory=cave-community/daioujou',
                     'ReadWritePaths=/var/lib/cave-community/daioujou',
                     'ProtectSystem=strict', 'NoNewPrivileges=true',
                     'ExecStart=/usr/bin/python3 -B -m server.games.daioujou --database '
                     '/var/lib/cave-community/daioujou/community.sqlite3 --port 18088'):
            self.assertIn(line, service.splitlines())
        backup = (DEPLOY / 'cave-daioujou-backup.service.in').read_text(encoding='utf-8')
        for token in ('After=cave-daioujou.service', '/opt/cave-community/daioujou-current/server/backup.py',
                      '--database /var/lib/cave-community/daioujou/community.sqlite3',
                      '--directory /var/lib/cave-community/daioujou/backups'):
            self.assertIn(token, backup)
        for name in ('espgaluda2', 'ketsui', 'sdoj', 'futari', '/opt/cave-community/current'):
            self.assertNotIn(name, service + backup)
        timer = (DEPLOY / 'cave-daioujou-backup.timer.in').read_text(encoding='utf-8')
        self.assertIn('OnCalendar=daily', timer)
        self.assertIn('Persistent=true', timer)


if __name__ == '__main__':
    unittest.main()
