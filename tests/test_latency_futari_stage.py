import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('latency_futari_stage', ROOT / 'tools/latency/stage_futari_probe.py')
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


class NativeProfileTests(unittest.TestCase):
    def test_native_settings_only_and_foreign_header_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            old = stage.ROOT
            try:
                stage.ROOT = Path(folder)
                profile = stage.ROOT / '.local/input-latency-v080-r1/native-profile'
                payload = profile / '00000001/systemdata.bin/savegame.bin'
                header = profile / 'Headers/00000001/systemdata.bin.header'
                payload.parent.mkdir(parents=True); header.parent.mkdir(parents=True)
                payload.write_bytes(bytes(8192)); header.write_bytes(bytes(328))
                (profile / 'unrelated-replay.bin').write_bytes(b'unrelated')
                self.assertEqual(stage.native_test_profile(profile), [payload, header])
                header.write_bytes(bytes(40960))
                with self.assertRaisesRegex(ValueError, 'do not import Xenia'):
                    stage.native_test_profile(profile)
            finally: stage.ROOT = old

    def test_player_profile_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Private native TEST'):
            stage.native_test_profile(ROOT / 'user-data/futari')


if __name__ == '__main__': unittest.main()
