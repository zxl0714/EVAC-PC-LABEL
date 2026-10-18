"""Pinned source/dependency/tool preparation boundaries use only authored data."""
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import project
import prepare_tools
import sdk_sources


class SdkToolTests(unittest.TestCase):
    def test_dependency_parser_accepts_pinned_https_shape(self):
        result = sdk_sources.modules_from_text('[submodule "library"]\npath = third_party/lib\nurl = https://github.com/owner/repo.git\n')
        self.assertEqual(result, [dict(name='library', path='third_party/lib', url='https://github.com/owner/repo.git')])

    def test_dependency_parser_rejects_paths_and_command_urls(self):
        for path, url in (('../outside', 'https://github.com/owner/repo.git'),
                          ('C:/outside', 'https://github.com/owner/repo.git'),
                          ('library', '!run something'), ('library', 'file:///local/repo')):
            with self.subTest(path=path, url=url):
                with self.assertRaises(ValueError):
                    sdk_sources.modules_from_text(f'[submodule "fixture"]\npath = {path}\nurl = {url}\n')

    def test_zip_extraction_keeps_authored_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'fixture.zip'
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr('bundle/bin/tool.txt', b'authored fixture')
            expanded = prepare_tools.extract({'archiveRoot':'bundle'}, archive, root / 'new')
            self.assertEqual((expanded / 'bin/tool.txt').read_bytes(), b'authored fixture')

    def test_extraction_refuses_existing_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(FileExistsError):
                prepare_tools.extract({'archiveRoot':''}, root / 'unused.zip', root)

    def test_zip_extraction_rejects_escape_and_links(self):
        for name in ('../outside', '/absolute', 'C:/drive', 'dir\\outside'):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / 'fixture.zip'
                with zipfile.ZipFile(archive, 'w') as output:
                    # Windows ZipInfo normally canonicalizes backslashes on
                    # construction; preserve the raw member for this fixture.
                    member = zipfile.ZipInfo(name)
                    member.filename = name
                    output.writestr(member, b'authored')
                with self.assertRaises(ValueError):
                    prepare_tools.extract({'archiveRoot':''}, archive, root / 'new')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'link.zip'
            with zipfile.ZipFile(archive, 'w') as output:
                link = zipfile.ZipInfo('link')
                link.external_attr = 0o120777 << 16
                output.writestr(link, '../outside')
            with self.assertRaises(ValueError):
                prepare_tools.extract({'archiveRoot':''}, archive, root / 'new')

    def test_tar_extraction_rejects_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'bad.tar.xz'
            with tarfile.open(archive, 'w:xz') as output:
                item = tarfile.TarInfo('../outside')
                item.size = 4
                output.addfile(item, io.BytesIO(b'test'))
            with self.assertRaises(ValueError):
                prepare_tools.extract({'archiveRoot':''}, archive, root / 'new')

    def test_source_audit_rejects_unpinned_gitlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            subprocess.run(['git', '-C', str(root), 'update-index', '--add', '--cacheinfo',
                            '160000,' + '1' * 40 + ',dependency'], check=True)
            with self.assertRaisesRegex(ValueError, 'source lock'):
                project.audit_source(root)

    def test_source_audit_accepts_locked_gitlink_but_not_changed_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / 'tools').mkdir()
            item = dict(name='rex-demo', path='third_party/rexglue/thirdparty/demo',
                        url='https://github.com/example/demo.git', commit='1' * 40)
            (root / '.gitmodules').write_text(
                f'[submodule "{item["name"]}"]\npath = {item["path"]}\nurl = {item["url"]}\n', encoding='utf-8')
            lock = root / 'tools/sdk-source.lock.json'
            lock.write_text(json.dumps({'submodules':[item]}), encoding='utf-8')
            subprocess.run(['git', '-C', str(root), 'update-index', '--add', '--cacheinfo',
                            f'160000,{item["commit"]},{item["path"]}'], check=True)
            self.assertEqual(project.audit_source(root)['files'], 2)
            item['commit'] = '2' * 40
            lock.write_text(json.dumps({'submodules':[item]}), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'source lock'):
                project.audit_source(root)


if __name__ == '__main__':
    unittest.main()
