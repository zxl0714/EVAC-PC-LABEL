"""Initialization helpers use authored headers and resources, never game files."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import project


class SourceBoundaryTests(unittest.TestCase):
    def test_output_does_not_replace_existing_report(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report.json'
            project.write_json_new(path, {'keep': True})
            before = path.read_bytes()
            with self.assertRaises(FileExistsError):
                project.write_json_new(path, {'keep': False})
            self.assertEqual(path.read_bytes(), before)

    def audit_fixture(self, root):
        subprocess.run(['git', 'init', '-q', str(root)], check=True)
        path = root / 'code.py'
        path.write_text('print("authored")\n', encoding='utf-8')
        return path

    def test_audit_accepts_code_and_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.audit_fixture(root)
            (root / 'inputs.json').write_text('{"file":"default.xex","sha256":"metadata-only"}')
            self.assertEqual(project.audit_source(root)['files'], 2)

    def test_audit_rejects_disguised_binary_and_credentials(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = self.audit_fixture(root)
            for data in (b'XEX2 body', b'MZ exe', b'\x7fELF body',
                         b'-----BEGIN ' + b'PRIVATE KEY-----'):
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    project.audit_source(root)

    def test_authored_paths_are_ascii_but_checkout_path_can_be_unicode(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'项目'
            root.mkdir()
            self.audit_fixture(root)
            self.assertEqual(project.audit_source(root)['files'],1)
            (root/'说明.txt').write_text('authored guide',encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'filenames must use ASCII'):
                project.audit_source(root)

    def test_audit_rejects_forced_game_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.audit_fixture(root)
            (root / '.gitignore').write_text('/game-data/\n')
            (root / 'game-data').mkdir()
            (root / 'game-data/resource.txt').write_text('authored data that must stay local')
            subprocess.run(['git', '-C', str(root), 'add', '-f', 'game-data/resource.txt'], check=True)
            with self.assertRaisesRegex(ValueError, 'Local data'):
                project.audit_source(root)


if __name__ == '__main__':
    unittest.main()
