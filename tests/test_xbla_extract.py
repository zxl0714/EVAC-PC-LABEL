"""Exercise the shipped PowerShell helper against authored STFS containers."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/Extract-Xbla.ps1'
SHELL = shutil.which('powershell.exe') if os.name == 'nt' else None


def fixture(*, boundary=False):
    first = 0xA000
    blocks = 171 if boundary else 3
    offset = lambda b: first + (b + 1 + b // 170 + (1 if b >= 170 else 0)) * 4096
    data = bytearray(offset(blocks - 1) + 4096)
    def put(at, value, size=4, order='big'):
        data[at:at+size] = value.to_bytes(size, order)
    data[:4] = b'LIVE'
    put(0x340, first); put(0x344, 0xD0000); put(0x360, 0x58410A78)
    data[0x379] = 0x24; data[0x37B] = 1
    put(0x37C, 1, 2, 'little'); put(0x395, blocks)
    table = offset(0)
    data[table:table+6] = b'module'; data[table+40] = 0x80 | 6
    put(table+50, 65535, 2)
    entry = table+64
    data[entry:entry+10] = b'sample.bin'; data[entry+40] = 10
    put(entry+44, 2, 3, 'little')
    put(entry+47, 169 if boundary else 1, 3, 'little')
    put(entry+50, 0, 2); put(entry+52, 4101)
    chain = [169, 170] if boundary else [1, 2]
    expected = b'A' * 4096 + b'hello'
    data[offset(chain[0]):offset(chain[0])+4096] = expected[:4096]
    data[offset(chain[1]):offset(chain[1])+5] = expected[4096:]
    def hash_offset(block):
        return first + (0 if block < 170 else block // 170 * 171 + 1) * 4096 + block % 170 * 24
    def rehash(block, next_block=0xFFFFFF):
        at = hash_offset(block)
        data[at:at+20] = hashlib.sha1(data[offset(block):offset(block)+4096]).digest()
        data[at+20] = 0x80
        put(at+21, next_block, 3)
    rehash(0); rehash(chain[0], chain[1]); rehash(chain[1])
    return data, expected, put, rehash, table, entry


@unittest.skipUnless(SHELL, 'Windows PowerShell 5.1 is required for the shipped helper')
class XblaExtractTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='evac-xbla-authored-')
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def run_helper(self, data, *, label='case', output=None, success=True):
        source = self.root / (label+'.package'); source.write_bytes(data)
        output = output or self.root / (label+' extracted')
        result = subprocess.run([SHELL, '-NoProfile', '-File', str(SCRIPT),
                                 '-InputPackage', str(source), '-OutputDir', str(output)],
                                capture_output=True, timeout=30)
        self.assertEqual(source.read_bytes(), data, 'Input was modified')
        if success:
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            return output, json.loads(result.stdout)
        self.assertNotEqual(result.returncode, 0, 'Malformed input was accepted')
        self.assertFalse(output.exists(), 'Rejected input left a published directory')
        self.assertFalse(list(self.root.glob(output.name+'.extract-*')), 'Invalid data reached staging')

    def test_exact_bytes_hashes_and_level_one_boundary(self):
        for boundary in (False, True):
            with self.subTest(boundary=boundary):
                data, expected, *_ = fixture(boundary=boundary)
                output, report = self.run_helper(data, label=str(boundary))
                self.assertEqual((output/'module/sample.bin').read_bytes(), expected)
                self.assertEqual(report['TitleId'], '58410A78')
                self.assertEqual(report['CheckedBlocks'], 3)
                self.assertEqual(report['TotalFileBytes'], len(expected))
                self.assertEqual(report['PackageSha256'], hashlib.sha256(data).hexdigest())
                self.assertEqual(report['Files'][0]['Sha256'], hashlib.sha256(expected).hexdigest())
                self.assertEqual({p.relative_to(output).as_posix() for p in output.rglob('*')},
                                 {'module', 'module/sample.bin'})

    def test_pirs_and_empty_file(self):
        data, _, put, rehash, _, entry = fixture()
        data[:4] = b'PIRS'; put(entry+44, 0, 3, 'little'); put(entry+52, 0); rehash(0)
        output, report = self.run_helper(data)
        self.assertEqual((output/'module/sample.bin').read_bytes(), b'')
        self.assertEqual(report['CheckedBlocks'], 1)

    def test_relative_paths_follow_powershell_location(self):
        # Set-Location does not change .NET's process-wide CurrentDirectory.
        selected = self.root / 'selected folder'
        selected.mkdir()
        data, expected, *_ = fixture()
        source = selected / 'input.package'
        source.write_bytes(data)
        quoted = lambda value: "'" + str(value).replace("'", "''") + "'"
        command = (f"Set-Location -LiteralPath {quoted(selected)}; "
                   f"& {quoted(SCRIPT)} -InputPackage '.\\input.package' -OutputDir '.\\extracted'")
        result = subprocess.run([SHELL, '-NoProfile', '-Command', command],
                                cwd=self.root, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        report = json.loads(result.stdout)
        self.assertEqual(Path(report['OutputDirectory']), selected / 'extracted')
        self.assertEqual((selected / 'extracted/module/sample.bin').read_bytes(), expected)
        self.assertFalse((self.root / 'extracted').exists())
        self.assertEqual(source.read_bytes(), data)

    def test_existing_output_is_untouched(self):
        data, *_ = fixture()
        source = self.root/'input'; source.write_bytes(data)
        output = self.root/'existing'; output.mkdir()
        sentinel = output/'keep'; sentinel.write_bytes(b'unchanged')
        result = subprocess.run([SHELL, '-NoProfile', '-File', str(SCRIPT),
                                 '-InputPackage', str(source), '-OutputDir', str(output)], capture_output=True, timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(sentinel.read_bytes(), b'unchanged')
        self.assertEqual(list(output.iterdir()), [sentinel])

    def test_bad_chain_hash_and_unsupported_layout_fail_before_writing(self):
        for case in ('hash', 'table-cycle', 'file-cycle', 'reused', 'long-chain', 'truncated', 'layout', 'magic', 'size'):
            with self.subTest(case=case):
                data, _, put, rehash, table, entry = fixture()
                if case == 'hash': data[-4096] ^= 1
                elif case == 'table-cycle': rehash(0, 0)
                elif case == 'file-cycle': rehash(1, 1)
                elif case == 'reused': put(entry+47, 0, 3, 'little'); rehash(0)
                elif case == 'long-chain': rehash(2, 1)
                elif case == 'truncated': del data[-1]
                elif case == 'layout': data[0x37B] = 0
                elif case == 'magic': data[:4] = b'CON '
                elif case == 'size': put(entry+52, 5); rehash(0)
                self.run_helper(data, label=case, success=False)

    def test_paths_are_windows_safe_and_parent_is_a_directory(self):
        for name in ('../escape', 'A:B', 'CON.txt', 'file.', 'NUL', 'LPT1', 'a\\b'):
            with self.subTest(name=name):
                data, _, _, rehash, _, entry = fixture()
                data[entry:entry+40] = bytes(40); data[entry:entry+len(name)] = name.encode()
                data[entry+40] = len(name); rehash(0)
                self.run_helper(data, label='path-'+str(len(list(self.root.iterdir()))), success=False)
        data, _, put, rehash, _, entry = fixture()
        put(entry+50, 1, 2); rehash(0)
        self.run_helper(data, label='self-parent', success=False)

    def test_case_insensitive_path_collision(self):
        data, _, put, rehash, table, entry = fixture()
        data[table+128:table+192] = data[entry:entry+64]
        data[table+128:table+138] = b'SAMPLE.BIN'; rehash(0)
        self.run_helper(data, success=False)

    def test_output_junction_is_rejected(self):
        target=self.root/'outside'; target.mkdir()
        alias=self.root/'linked'
        command="New-Item -ItemType Junction -Path '"+str(alias)+"' -Target '"+str(target)+"' | Out-Null"
        subprocess.run([SHELL, '-NoProfile', '-Command', command], check=True, capture_output=True)
        try:
            data, *_=fixture()
            self.run_helper(data, output=alias/'new', success=False)
            self.assertEqual(list(target.iterdir()), [])
        finally:
            alias.rmdir()


if __name__ == '__main__':
    unittest.main()
