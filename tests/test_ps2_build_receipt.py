import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import ps2_build_receipt as receipt


class Ps2ReceiptTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix='evac-ps2-receipt-')
        self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)

    def test_generated_identity_tracks_bodies_names_and_additions_but_not_mtime(self):
        source=self.root/'generated';source.mkdir()
        a=source/'a.cpp';a.write_text('void authored_a() {}\n')
        (source/'table.h').write_text('void authored_a();\n')
        before=receipt.tree_identity(source)
        self.assertEqual(before['files'],2)
        a.touch()
        self.assertEqual(receipt.tree_identity(source),before)
        a.write_text('void authored_a() { return; }\n')
        self.assertNotEqual(receipt.tree_identity(source)['sha256'],before['sha256'])
        a.write_text('void authored_a() {}\n')
        a.rename(source/'renamed.cpp')
        self.assertNotEqual(receipt.tree_identity(source)['sha256'],before['sha256'])
        a.write_text('void authored_extra() {}\n')
        self.assertEqual(receipt.tree_identity(source)['files'],3)
        with self.assertRaises(ValueError):receipt.tree_identity(self.root/'empty')

    def test_configuration_requires_release_symbols_lto_and_pinned_tools(self):
        tools={name:self.root/name for name in ('clang-cl','ninja')}
        values=dict(CMAKE_C_COMPILER=str(tools['clang-cl']),CMAKE_CXX_COMPILER=str(tools['clang-cl']),
                    CMAKE_MAKE_PROGRAM=str(tools['ninja']),CMAKE_BUILD_TYPE='Release',
                    PS2_GAME_SYMBOLS='ON',PS2_RUNTIME_THINLTO='ON')
        def save(changes=None):
            state=dict(values,**(changes or {}))
            (self.root/'CMakeCache.txt').write_text(''.join(k+':STRING='+v+'\n' for k,v in state.items()))
        with patch.object(receipt,'tool_path',side_effect=lambda name,root:tools[name]):
            save();self.assertEqual(receipt.configuration(self.root),values)
            for key,value in (('PS2_GAME_SYMBOLS','OFF'),('PS2_RUNTIME_THINLTO','OFF'),
                              ('CMAKE_BUILD_TYPE','Debug'),('CMAKE_CXX_COMPILER','outside/clang-cl')):
                save({key:value})
                with self.subTest(key=key),self.assertRaises(ValueError):receipt.configuration(self.root)

    def test_verifier_rejects_stale_sources_missing_symbols_and_changed_outputs(self):
        path=self.root/'receipt.json';snapshot=dict(sha256='authored-snapshot')
        outputs=[]
        for name in receipt.OUTPUTS:
            file=self.root/'out/ps2-tools/bin'/name;file.parent.mkdir(parents=True,exist_ok=True)
            file.write_bytes(('authored '+name).encode())
            outputs.append(dict(path=file.relative_to(self.root).as_posix(),bytes=file.stat().st_size,
                                sha256=receipt.digest(file)))
        record=dict(format=1,backend='ps2',games=['espgaluda'],sourceSnapshot=snapshot,outputs=outputs)
        def save(value):path.write_text(json.dumps(value))
        with patch.object(receipt,'ROOT',self.root),patch.object(receipt,'RECEIPT',path), \
             patch.object(receipt,'snapshot',return_value=snapshot):
            save(record);self.assertEqual(receipt.verify(),record)
            save(dict(record,sourceSnapshot=dict(sha256='stale')))
            with self.assertRaises(ValueError):receipt.verify()
            save(dict(record,outputs=outputs[:-1]))
            with self.assertRaises(ValueError):receipt.verify()
            save(record);(self.root/outputs[0]['path']).write_bytes(b'changed')
            with self.assertRaises(ValueError):receipt.verify()


if __name__=='__main__':unittest.main()
