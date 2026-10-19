"""Shared driver resolution is pinned, versioned and independent of legacy tools."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest
import toolchain


class SharedToolchainTests(unittest.TestCase):
    def fixture(self, root):
        directory = root / 'tools/apps/llvm-fixture'
        directory.mkdir(parents=True)
        files = {}
        for name in ('bin/clang.exe', 'bin/clang++.exe', 'bin/clang-cl.exe', 'runtime.lib'):
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('authored fixture: ' + name, encoding='utf-8')
            files[name] = digest(path)
        package = dict(id='llvm', version='fixture', installDirectory='llvm-fixture',
                       archiveRoot='', url='https://example.invalid/tool.zip', sha256='a' * 64,
                       entry='bin/clang++.exe', entrySha256=files.pop('bin/clang++.exe'),
                       requiredFiles=files)
        lock = root / 'tools/packages.lock.json'
        lock.write_text(json.dumps(dict(format=1, platform='windows-x64', packages=[package])), encoding='utf-8')
        return lock, package, directory

    def test_current_lock_has_one_llvm_for_both_driver_styles(self):
        packages = toolchain.locked_packages(ROOT)
        llvm, = (package for package in packages if package['id'] == 'llvm')
        self.assertEqual(llvm['version'], '23.1.3')
        self.assertEqual(toolchain.TOOLS['clang++'][0], toolchain.TOOLS['clang-cl'][0])
        self.assertIn('bin/clang-cl.exe', llvm['requiredFiles'])
        self.assertIn('bin/lld-link.exe', llvm['requiredFiles'])
        self.assertIn('lib/clang/23/lib/windows/clang_rt.builtins-x86_64.lib', llvm['requiredFiles'])
        self.assertFalse((ROOT / 'tools/ps4-tools.lock.json').exists())

    def test_drivers_resolve_to_one_version_and_ignore_legacy_installations(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            _, _, directory = self.fixture(root)
            legacy = root / 'tools/apps/llvm/bin/clang.exe'
            legacy.parent.mkdir(parents=True)
            legacy.write_text('preserve old compiler')
            self.assertEqual(toolchain.tool_path('clang', root).parent, directory / 'bin')
            self.assertEqual(toolchain.tool_path('clang-cl', root).parent, directory / 'bin')
            self.assertEqual(legacy.read_text(), 'preserve old compiler')
            self.assertEqual(len(toolchain.tool_snapshot(root)), 4)

    def test_mutated_driver_cannot_be_authorized_by_an_install_receipt(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            _, _, directory = self.fixture(root)
            path = directory / 'bin/clang-cl.exe'
            path.write_text('changed binary')
            receipt = root / 'tools/state/installed.json'
            receipt.parent.mkdir(parents=True)
            receipt.write_text(json.dumps({'entrySha256': digest(path)}))
            with self.assertRaisesRegex(ValueError, 'component differs'):
                toolchain.tool_path('clang-cl', root)
            self.assertEqual(path.read_text(), 'changed binary')

    def test_snapshot_checks_required_runtime_not_just_driver(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            _, _, directory = self.fixture(root)
            (directory / 'runtime.lib').write_text('changed runtime')
            with self.assertRaisesRegex(ValueError, 'component differs'):
                toolchain.tool_snapshot(root)

    def test_unpinned_tool_has_no_path_fallback(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.fixture(root)
            with self.assertRaisesRegex(ValueError, 'not pinned'):
                toolchain.tool_path('lld-link', root)

    def test_installation_paths_are_bounded_and_not_shared_by_packages(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            lock, package, _ = self.fixture(root)
            for name in ('../outside', 'C:/outside', '.', 'sub/directory', 'sub\\directory'):
                invalid = dict(package, installDirectory=name)
                lock.write_text(json.dumps(dict(format=1, platform='windows-x64', packages=[invalid])))
                with self.subTest(name=name), self.assertRaises(ValueError):
                    toolchain.locked_packages(root)
            second = dict(package, id='other')
            lock.write_text(json.dumps(dict(format=1, platform='windows-x64', packages=[package, second])))
            with self.assertRaisesRegex(ValueError, 'Duplicate tool installation'):
                toolchain.locked_packages(root)

    def test_receipts_are_bound_to_lock_and_do_not_replace_old_receipts(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            lock, package, _ = self.fixture(root)
            before = toolchain.receipt_path(root)
            before.parent.mkdir(parents=True)
            before.write_text('preserve previous tool metadata')
            package['version'] = 'next-fixture'
            lock.write_text(json.dumps(dict(format=1, platform='windows-x64', packages=[package])))
            self.assertNotEqual(toolchain.receipt_path(root), before)
            self.assertEqual(before.read_text(), 'preserve previous tool metadata')


if __name__ == '__main__':
    unittest.main()
