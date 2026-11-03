import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from package_receipt import PackageInputs


class PackageReceiptTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.git('init', '-b', 'main')
        self.git('config', 'user.email', 'test@example.invalid')
        self.git('config', 'user.name', 'Fixture')
        for name in ('backends/ps2/runtime.cpp', 'backends/ps4/input/pad.cpp',
                     'common/shared.h', 'launcher/main.cpp', 'README.md'):
            self.write(name, 'authored input\n')
        self.save()
        self.origin = self.git('rev-parse', 'HEAD').strip().decode()
        self.record = self.root/'receipt.json'
        # Keep certificate outside the tracked checkout, just like out/.
        self.git('config', 'core.excludesfile', str(self.root/'ignore'))
        (self.root/'ignore').write_text('receipt.json\nignore\n')

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args], stderr=subprocess.DEVNULL)

    def write(self, name, value):
        path = self.root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value)

    def save(self):
        self.git('add', '.')
        self.git('commit', '-m', 'fixture')

    def receipt(self, backend='ps2'):
        files = ('backends/ps2/runtime.cpp', 'backends/ps4/input/pad.cpp', 'common/shared.h')
        if backend == 'xbox360':
            files += ('README.md',)
        source = dict(files={name:hashlib.sha256((self.root/name).read_bytes()).hexdigest() for name in files},
                      generated={'private': 'unchanged build evidence'}, tools={'private': 'build tool identity'})
        source['sha256'] = hashlib.sha256(json.dumps(source, sort_keys=True).encode()).hexdigest()
        record = dict(format=1, backend=backend, sourceCommit=self.origin, sourceDirty=False,
                      sourceSnapshot=source, outputs=[dict(path='out/ps2-tools/bin/espgaluda.exe', bytes=10, sha256='a'*64)])
        if backend == 'xbox360':
            record.pop('backend');record['kind']='host'
            record['outputs'][0]['path']='futari.exe'
        self.record.write_text(json.dumps(record))
        return record

    def test_docs_commit_reuses_original_build_without_generated_files_or_symbols(self):
        original = self.receipt()
        self.write('README.md', 'updated docs');self.save()
        inputs = PackageInputs(self.root)
        result = inputs.load(self.record, 'ps2')
        self.assertEqual(result['sourceCommit'], self.origin)
        self.assertNotEqual(inputs.commit, self.origin)
        self.assertEqual(result, original)

    def test_relevant_edit_delete_and_new_nested_source_require_rebuild(self):
        self.receipt()
        self.write('common/shared.h', 'changed shared input');self.save()
        with self.assertRaisesRegex(ValueError, 'Rebuild ps2'):
            PackageInputs(self.root).load(self.record, 'ps2')
        self.git('reset', '--hard', self.origin)
        self.write('backends/ps2/new-directory/new.cpp', 'new implementation');self.save()
        with self.assertRaisesRegex(ValueError, 'Rebuild ps2'):
            PackageInputs(self.root).load(self.record, 'ps2')
        self.git('reset', '--hard', self.origin)
        (self.root/'backends/ps2/runtime.cpp').unlink();self.save()
        with self.assertRaisesRegex(ValueError, 'Rebuild ps2'):
            PackageInputs(self.root).load(self.record, 'ps2')

    def test_build_from_dirty_source_is_not_reused_as_clean(self):
        self.write('common/shared.h', 'temporary source used for build')
        self.receipt()
        self.git('restore', 'common/shared.h')
        with self.assertRaisesRegex(ValueError, 'source differs'):
            PackageInputs(self.root).load(self.record, 'ps2')

    def test_historical_host_whole_repository_scope_excludes_ps2_only_changes(self):
        self.receipt('xbox360')
        self.write('backends/ps2/runtime.cpp', 'new PS2 implementation');self.save()
        PackageInputs(self.root).load(self.record, 'xbox360')
        self.write('common/shared.h', 'shared implementation');self.save()
        with self.assertRaisesRegex(ValueError, 'Rebuild xbox360'):
            PackageInputs(self.root).load(self.record, 'xbox360')

    def test_shared_ps4_input_used_by_ps2_is_not_ignored(self):
        self.receipt()
        self.write('backends/ps4/input/pad.cpp', 'new pad');self.save()
        with self.assertRaisesRegex(ValueError, 'Rebuild ps2'):
            PackageInputs(self.root).load(self.record, 'ps2')

    def test_inconsistent_receipt_and_output_traversal_are_rejected(self):
        record = self.receipt()
        record['sourceSnapshot']['files']['common/shared.h']='changed'
        self.record.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'Inconsistent'):
            PackageInputs(self.root).load(self.record, 'ps2')
        record = self.receipt()
        record['outputs'][0]['path']='out/../private.exe'
        self.record.write_text(json.dumps(record))
        with self.assertRaises(ValueError):
            PackageInputs(self.root).load(self.record, 'ps2')

    def test_dirty_worktree_rejected_before_receipt_read(self):
        self.write('common/shared.h', 'dirty')
        with self.assertRaisesRegex(ValueError, 'Commit source'):
            PackageInputs(self.root)
