import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from build_data import build_data


class BuildDataTests(unittest.TestCase):
    def test_internal_user_data_config_without_legacy_file(self):
        with tempfile.TemporaryDirectory(prefix='cave-build-data-') as temporary:
            package = Path(temporary); source = package / 'source'; source.mkdir()
            (package / 'release.json').write_text('{}')
            (package / 'user-data').mkdir()
            (package / 'user-data/config.ini').write_text('[collection]\nconfig_version=1\n')
            data = package / 'game-data/futari'; data.mkdir(parents=True)
            response = json.dumps(dict(games=[dict(dataState='verified', problems=[], dataDirectory=str(data))])).encode()
            with patch('build_data.subprocess.check_output', return_value=response) as called:
                self.assertEqual(build_data('futari', root=source), data)
                self.assertIn(str(package), called.call_args.args[0])

    def test_empty_checkout_and_explicit_priority(self):
        with tempfile.TemporaryDirectory(prefix='cave-build-data-') as temporary:
            root = Path(temporary)
            self.assertEqual(build_data('futari', root=root), root / 'game-data/futari')
            explicit = root / 'own data'; explicit.mkdir()
            with patch('build_data.subprocess.check_output', side_effect=AssertionError('no INI needed')):
                self.assertEqual(build_data('futari', explicit, root), explicit)

    def test_internal_source_uses_package_native_config_reader(self):
        with tempfile.TemporaryDirectory(prefix='cave-build-data-') as temporary:
            package = Path(temporary); source = package / 'source'; source.mkdir()
            data = package / 'game-data/futari'; data.mkdir(parents=True)
            (package / 'release.json').write_text('{}'); (package / 'config.ini').write_text('[collection]\nconfig_version=1\n')
            response = json.dumps(dict(games=[dict(dataState='verified', problems=[], dataDirectory=str(data))])).encode()
            with patch('build_data.subprocess.check_output', return_value=response) as called:
                self.assertEqual(build_data('futari', root=source), data)
                self.assertEqual(called.call_args.args[0][0], str(package / 'runtime/launcher/cave_inspect.exe'))
                self.assertIn(str(package), called.call_args.args[0])

    def test_bad_configured_data_does_not_fall_back(self):
        with tempfile.TemporaryDirectory(prefix='cave-build-data-') as temporary:
            root = Path(temporary); (root / 'config.ini').write_text('[collection]\nconfig_version=1\n')
            inspector = root / 'out/common/bin/cave_inspect.exe'; inspector.parent.mkdir(parents=True); inspector.write_bytes(b'fixture')
            response = json.dumps(dict(games=[dict(dataState='missing_data', problems=['absent'])])).encode()
            with patch('build_data.subprocess.check_output', return_value=response), self.assertRaises(ValueError):
                build_data('futari', root=root)
