import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import build_receipt
import project
import sdk_sources


class DependencyTests(unittest.TestCase):
    def test_path_and_url_validation(self):
        for value in ('', '../a', 'a/../b', '/a', 'C:/a', 'a\\b', 'a//b', '.git/x'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                sdk_sources.safe_relative(value)
        for url in ('ssh://github.com/a/b', 'https://evil.test/a/b', 'https://github.com/a/b?x=1'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                sdk_sources.modules_from_text(f'[submodule "x"]\npath = deps/a\nurl = {url}\n')

    def test_duplicate_module_paths(self):
        with self.assertRaises(ValueError):
            sdk_sources.modules_from_text(
                '[submodule "a"]\npath = deps/a\nurl = https://github.com/a/a\n'
                '[submodule "b"]\npath = DEPS/A\nurl = https://github.com/a/b\n')

    def fixture(self, root):
        subprocess.run(['git', 'init', '-q', str(root)], check=True)
        item = dict(name='rex-demo', path='third_party/rexglue/thirdparty/demo',
                    url='https://github.com/example/demo.git', commit='1' * 40)
        (root / '.gitmodules').write_text(
            f'[submodule "{item["name"]}"]\npath = {item["path"]}\nurl = {item["url"]}\n', encoding='utf-8')
        (root / 'tools').mkdir()
        (root / 'tools/sdk-source.lock.json').write_text(json.dumps(dict(submodules=[item])), encoding='utf-8')
        subprocess.run(['git', '-C', str(root), 'update-index', '--add', '--cacheinfo',
                        f'160000,{item["commit"]},{item["path"]}'], check=True)
        return item

    def test_audit_skips_only_pinned_gitlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            item = self.fixture(root)
            result = project.audit_source(root)
            self.assertEqual(result['submodules'], 1)
            self.assertEqual(result['files'], 2)
            subprocess.run(['git', '-C', str(root), 'update-index', '--cacheinfo',
                            f'160000,{"2" * 40},{item["path"]}'], check=True)
            with self.assertRaisesRegex(ValueError, 'source lock'):
                project.audit_source(root)

    def test_lock_and_module_declarations_must_match(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            path = root / '.gitmodules'
            path.write_text(path.read_text(encoding='utf-8').replace('example/demo', 'example/other'), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'lock differs'):
                sdk_sources.locked_modules(root)

    def test_build_receipt_rejects_unpinned_dependencies(self):
        valid = ' ' + 'a' * 40 + ' third_party/demo (v1)'
        self.assertEqual(build_receipt.dependency_ids([valid])[0]['commit'], 'a' * 40)
        for line in ('-' + valid[1:], '+' + valid[1:], 'U' + valid[1:]):
            with self.subTest(line=line), self.assertRaises(ValueError):
                build_receipt.dependency_ids([line])

    def test_build_receipt_reads_matching_pe_pdb_identities(self):
        guid = '01234567-89ab-cdef-0123-456789abcdef'
        self.assertEqual(build_receipt.signature(f'PDBGUID: {{{guid}}}\nPDBAge: 1'),
                         build_receipt.signature(f'GUID: {{{guid.upper()}}}\nAge: 1', True))
        with self.assertRaises(ValueError):
            build_receipt.signature('no debug identity')


if __name__ == '__main__':
    unittest.main()
