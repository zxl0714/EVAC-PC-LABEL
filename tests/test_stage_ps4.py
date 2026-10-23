import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import stage_ps4 as module


class Ps4StageTests(unittest.TestCase):
    def test_shared_host_is_staged_with_distinct_title_manifests(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            outputs = []
            for stem in ('out/ps4-native/KetsuiNativeProbe.exe', 'out/ps4-gpu/KetsuiGpuRuntime.dll'):
                for extension in (None, '.pdb'):
                    path = root / stem
                    if extension:
                        path = path.with_suffix(extension)
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(('authored-' + path.name).encode())
                    outputs.append(dict(path=path.relative_to(root).as_posix(), bytes=path.stat().st_size,
                                        sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
            receipt = dict(sourceCommit='a' * 40, sourceSnapshot=dict(sha256='b' * 64), outputs=outputs)
            with patch.object(module, 'ROOT', root), patch.object(module, 'verify', return_value=receipt):
                self.assertEqual(module.stage()['games'], ['ketsui', 'daioujou', 'esprade'])
                for game, peer in [('ketsui', 'daioujou'), ('daioujou', 'ketsui'), ('esprade', 'daioujou')]:
                    directory = root / 'runtime/games' / game
                    manifest = json.loads((directory / 'runtime-manifest.json').read_text())
                    self.assertEqual(manifest['game'], game)
                    self.assertFalse(manifest['gameDataIncluded'])
                    self.assertEqual(len(manifest['files']), 4)
                    paths = {item['path'] for item in manifest['files']}
                    self.assertIn(f'runtime/ps4/{game}.exe', paths)
                    self.assertIn('runtime/ps4/KetsuiGpuRuntime.dll', paths)
                    self.assertNotIn(f'runtime/ps4/{peer}.exe', paths)
                    text = (directory / 'runtime-manifest.ini').read_text()
                    self.assertIn(f'game = {game}', text)
                    self.assertNotIn(f'runtime/ps4/{peer}.exe', text)
                self.assertEqual((root / 'runtime/ps4/ketsui.exe').read_bytes(),
                                 (root / 'runtime/ps4/daioujou.exe').read_bytes())
                self.assertEqual((root / 'runtime/ps4/esprade.exe').read_bytes(),
                                 (root / 'runtime/ps4/ketsui.exe').read_bytes())
                self.assertEqual(module.stage()['stagedFiles'], 11)
                self.assertEqual(len(list((root / '.local/staging').glob('*-previous.json'))), 3)


if __name__ == '__main__':
    unittest.main()
