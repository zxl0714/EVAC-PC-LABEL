import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import release_package as release
import source_package as source


class SourcePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cave-authored-source-package-')
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def fixture(self):
        files = {'README.txt': b'authored instructions', 'source/.git/HEAD': b'ref: refs/heads/main\n',
                 'source/FOR_AGENT.md': b'authored development instructions',
                 'source/tools/sdk-source.lock.json': b'{}', 'source/tools/ps4-source.lock.json': b'{}'}
        record = dict(format=1, version='0.3-test', sourceCommit='1' * 40, gameDataIncluded=False,
                      binaryReleaseSha256='2' * 64, binaryFlavor='Public',
                      files=[dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                             for name, data in files.items()])
        return record, files

    def zipped(self, record, files, name='authored.zip'):
        archive = self.root / name
        prefix = source.source_name(record['version']) + '/'
        with zipfile.ZipFile(archive, 'x') as package:
            package.writestr(prefix + 'source-release.json', json.dumps(record))
            for name, data in files.items():
                package.writestr(prefix + name, data)
        return archive

    def test_zip_verifier_checks_authored_members_and_streamed_hashes(self):
        record, files = self.fixture()
        result = source.verify(self.zipped(record, files))
        self.assertTrue(result['sourceZipVerified'])
        self.assertEqual(result['sourceCommit'], record['sourceCommit'])
        self.assertFalse(result['gameDataIncluded'])
        files['source/FOR_AGENT.md'] = b'changed'
        with self.assertRaisesRegex(ValueError, 'differs'):
            source.verify(self.zipped(record, files, 'changed.zip'))

    def test_v04_source_archive_uses_the_same_product_name_as_binary_packages(self):
        record, files = self.fixture(); record['version'] = '0.4'
        self.assertEqual(source.source_name('0.4'), 'EVAC-PC-LABEL-v0.4-source')
        result = source.verify(self.zipped(record, files))
        self.assertTrue(result['sourceZipVerified'])
        self.assertEqual(result['version'], '0.4')

    def test_local_bodies_tools_and_invalid_identities_are_rejected(self):
        for name in ('source/game-data/daioujou/eboot.bin', 'source/generated/body.cpp',
                     'source/prepared/daioujou/eboot.ktprobe', 'source/config.ini',
                     'source/.deps/sdk-install/bin/tool.exe', 'source/tools/apps/tool.exe',
                     '../escape', 'game-data/main.bin'):
            record, _ = self.fixture()
            record['files'].append(dict(path=name, bytes=0, sha256=hashlib.sha256(b'').hexdigest()))
            with self.subTest(name=name), self.assertRaises(ValueError):
                source.check_record(record)
        for key, value in (('gameDataIncluded', True), ('sourceCommit', 'wrong'),
                           ('binaryFlavor', 'Source'), ('version', '0.5')):
            record, _ = self.fixture()
            record[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                source.check_record(record)

    def test_missing_or_unlisted_members_and_duplicates_are_rejected(self):
        record, files = self.fixture()
        files['source/extra.txt'] = b'not inventoried'
        with self.assertRaisesRegex(ValueError, 'member set'):
            source.verify(self.zipped(record, files))
        record['files'].append(record['files'][0])
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            source.check_record(record)

    def test_build_reuses_independent_clone_then_checks_actual_zip_and_extraction(self):
        repo = self.root / 'project'
        repo.mkdir()
        for name, value in {'.gitignore': 'releases/\n', 'FOR_AGENT.md': 'authored guide',
                            'tools/sdk-source.lock.json': '{}', 'tools/ps4-source.lock.json': '{}'}.items():
            file = repo / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(value, encoding='utf-8')
        for arguments in (('init', '-b', 'main'), ('config', 'user.name', 'Fixture'),
                          ('config', 'user.email', 'fixture@example.invalid'),
                          ('add', '.'), ('commit', '-m', 'Authored source')):
            subprocess.run(['git', '-C', str(repo), *arguments], check=True, capture_output=True)
        commit = release.git(repo, 'rev-parse', 'HEAD').decode().strip()
        binary = self.root / 'binary'
        binary.mkdir()
        (binary / 'release.json').write_text('{}', encoding='utf-8')
        receipt = dict(version='0.3-test', sourceCommit=commit, flavor='Public')
        with patch.object(source, 'ROOT', repo), patch.object(release, 'ROOT', repo), \
                patch.object(release, 'verify_tree', return_value=receipt) as binary_check, \
                patch.object(source, 'audit_source'), patch.object(release, 'audit_source'), \
                patch.object(source.sdk_sources, 'restore', return_value=25) as sdk, \
                patch.object(source.ps4_sources, 'restore', return_value=dict(verifiedDependencies=19)) as ps4:
            result = source.build(binary, repo / 'releases/source-test')
            binary_check.return_value = dict(receipt, sourceCommit='0' * 40)
            with self.assertRaisesRegex(ValueError, 'match the binary release commit'):
                source.build(binary, repo / 'releases/wrong-commit')
            self.assertFalse((repo / 'releases/wrong-commit').exists())
        sdk.assert_called_once()
        ps4.assert_called_once()
        archive = Path(result['archive'])
        self.assertTrue(result['sourceZipVerified'])
        self.assertFalse(result['uploaded'])
        restored = self.root / '解压 source with spaces'
        with zipfile.ZipFile(archive) as zipped:
            zipped.extractall(restored)
        checkout = restored / 'CAVE-PC-Collection-v0.3-test-source/source'
        self.assertEqual(release.git(checkout, 'rev-parse', 'HEAD').decode().strip(), commit)
        self.assertEqual(release.git(checkout, 'status', '--porcelain'), b'')
        self.assertFalse((checkout / '.git/objects/info/alternates').exists())
        self.assertEqual(release.git(checkout, 'remote', 'get-url', 'origin').decode().strip(), release.REMOTE)
        self.assertTrue(archive.with_suffix('.zip.sha256').read_text().startswith(release.digest(archive)))

    def test_windows_package_does_not_create_a_source_attachment(self):
        for package_format in (2, 3):
            with self.subTest(format=package_format), \
                    patch.object(release, 'verify_tree', return_value=dict(format=package_format)), \
                    patch.object(release, 'source_clone', side_effect=AssertionError('No source clone')):
                with self.assertRaisesRegex(ValueError, 'no source attachment'):
                    source.build(self.root, self.root/'not-created')
        self.assertFalse((self.root/'not-created').exists())


if __name__ == '__main__':
    unittest.main()
