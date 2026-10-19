import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import ps4_build_receipt as module


class Ps4BuildReceiptTests(unittest.TestCase):
    def fixture(self, root):
        for component, prefix in (('gcn', ''), ('gpu_core', 'upstream')):
            directory = root / 'third_party/ps4' / component
            path = directory / prefix / 'src/fixture.h'
            path.parent.mkdir(parents=True)
            path.write_bytes(b'authored source')
            inventory = dict(files=[dict(path='src/fixture.h', sha256=module.digest(path))])
            (directory / 'upstream-files.json').write_text(json.dumps(inventory))

    def test_current_upstream_and_maintained_changes_are_verified(self):
        self.assertEqual(module.upstream(), 383)

    def test_changed_unknown_duplicate_and_escaping_inventory_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            path = root / 'third_party/ps4/gcn/src/fixture.h'
            inventory = root / 'third_party/ps4/gcn/upstream-files.json'
            self.assertEqual(module.upstream(root), 2)
            path.write_bytes(b'changed source')
            with self.assertRaisesRegex(ValueError, 'differs'):
                module.upstream(root)
            data = json.loads(inventory.read_text())
            change = dict(path='src/fixture.h', sha256=module.digest(path))
            data['localChanges'] = [change]
            inventory.write_text(json.dumps(data))
            self.assertEqual(module.upstream(root), 2)
            for values in ([change, change], [dict(change, path='src/unknown.h')],
                           [dict(change, path='../escape.h')]):
                data['localChanges'] = values
                inventory.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    module.upstream(root)

    def test_binary_and_symbols_require_matching_guid_and_age(self):
        guid = '12345678-1234-1234-1234-123456789abc'
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = root / 'out/fixture'
            folder.mkdir(parents=True)
            (folder / 'fixture.exe').write_bytes(b'authored PE placeholder')
            (folder / 'fixture.pdb').write_bytes(b'authored PDB placeholder')
            pe = f'PDBGUID: {{{guid}}}\nPDBAge: 1'.encode()
            pdb = f'GUID: {{{guid}}}\nAge: 1'.encode()
            with patch.object(module, 'ROOT', root), patch.object(module, 'OUTPUTS', {'out/fixture':('fixture',)}), \
                 patch.object(module, 'tool_path', side_effect=lambda name, selected: root / 'authored-tools' / name):
                with patch.object(module.subprocess, 'check_output', side_effect=[pe, pdb]):
                    outputs = module.completed_outputs()
                self.assertEqual(len(outputs), 2)
                self.assertEqual(outputs[0]['sha256'], hashlib.sha256(b'authored PE placeholder').hexdigest())
                with patch.object(module.subprocess, 'check_output', side_effect=[pe, pdb.replace(b'Age: 1', b'Age: 2')]):
                    with self.assertRaisesRegex(ValueError, 'identity differs'):
                        module.completed_outputs()

    def test_verification_rejects_source_output_or_inventory_changes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            receipt = root / 'receipt.json'
            output = root / 'out/fixture'
            output.mkdir(parents=True)
            for name in ('fixture.exe', 'fixture.pdb'):
                (output / name).write_bytes(b'authored output')
            record = dict(format=1, backend='ps4', sourceSnapshot={'sha256':'fixed'}, outputs=[
                dict(path='out/fixture/'+name, sha256=module.digest(output / name))
                for name in ('fixture.exe', 'fixture.pdb')])
            receipt.write_text(json.dumps(record))
            with patch.object(module, 'ROOT', root), patch.object(module, 'RECEIPT', receipt), \
                 patch.object(module, 'OUTPUTS', {'out/fixture':('fixture',)}), \
                 patch.object(module, 'snapshot', return_value={'sha256':'fixed'}):
                self.assertEqual(module.verify(), record)
                (output / 'fixture.exe').write_bytes(b'changed output')
                with self.assertRaisesRegex(ValueError, 'output differs'):
                    module.verify()
                record['outputs'].pop()
                receipt.write_text(json.dumps(record))
                with self.assertRaisesRegex(ValueError, 'Incomplete'):
                    module.verify()
                record['sourceSnapshot']['sha256'] = 'changed'
                receipt.write_text(json.dumps(record))
                with self.assertRaisesRegex(ValueError, 'sources/tools changed'):
                    module.verify()


if __name__ == '__main__':
    unittest.main()
