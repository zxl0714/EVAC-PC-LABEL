import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import urlopen
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('futari_local_service', ROOT / 'scripts/futari/local_service.py')
local = importlib.util.module_from_spec(spec)
spec.loader.exec_module(local)


class FutariLocalServiceTests(unittest.TestCase):
    def test_new_local_database_and_collection_route(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(local, 'private_output', side_effect=Path):
            folder = Path(temp) / 'new'
            server, info = local.create_service(folder)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                self.assertEqual(server.server_address[0], '127.0.0.1')
                self.assertTrue(info['localOnly'])
                self.assertEqual(json.loads((folder / 'service.json').read_text()), info)
                with urlopen(info['endpoint'] + '/games/futari/health', timeout=3) as response:
                    self.assertEqual(json.load(response)['title_id'], '435A07D7')
                with self.assertRaises(HTTPError) as wrong:
                    urlopen(info['endpoint'] + '/games/espgaluda2/health', timeout=3)
                self.assertEqual(wrong.exception.code, 404)
                with self.assertRaises(FileExistsError):
                    local.create_service(folder)
            finally:
                server.shutdown()
                server.server_close()
                thread.join(3)
                self.assertFalse(thread.is_alive())

    def test_output_boundaries_and_ports(self):
        with self.assertRaises(ValueError):
            local.create_service(ROOT / 'game-data' / 'forbidden-local-service')
        with self.assertRaises(ValueError):
            local.create_service(ROOT / '.local' / 'unused-service', -1)
        with self.assertRaises(ValueError):
            local.create_service(ROOT / '.local' / 'unused-service', 65536)


if __name__ == '__main__':
    unittest.main()
