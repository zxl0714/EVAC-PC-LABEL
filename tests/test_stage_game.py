from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import stage_game
from project import digest


class StageGameTests(unittest.TestCase):
    def fixture(self,root,game,build):
        names=[f'runtime/xbox360/{game}.exe','runtime/xbox360/rexruntime.dll',
               f'runtime/xbox360/{game}_crash_writer.exe',f'runtime/games/{game}/runtime-manifest.ini']
        outputs=[]
        for relative in names[:-1]:
            target=Path(relative)
            for name in (target.name,target.with_suffix('.pdb').name):
                source=build/name
                source.write_bytes(('authored '+name).encode())
                outputs.append(dict(path=name,bytes=source.stat().st_size,sha256=digest(source)))
        return dict(id=game,runtimeFiles=names),dict(outputs=outputs,sourceCommit='a'*40,
            sourceSnapshot=dict(sha256='b'*64))

    def test_two_games_keep_unique_helpers_and_do_not_rewrite_identical_sdk(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);build=root/'build';build.mkdir()
            with patch.object(stage_game,'ROOT',root):
                first,first_receipt=self.fixture(root,'espgaluda2',build)
                stage_game.stage_one(first,first_receipt,build)
                certificate=root/'runtime/games/espgaluda2/runtime-manifest.ini'
                first_certificate=certificate.read_bytes()
                second,second_receipt=self.fixture(root,'futari',build)
                real_copy=stage_game.shutil.copy2
                with patch.object(stage_game.shutil,'copy2',wraps=real_copy) as copy:
                    result=stage_game.stage_one(second,second_receipt,build)
                rewritten={Path(call.args[1]).name for call in copy.call_args_list}
                self.assertNotIn('rexruntime.dll',rewritten)
                self.assertNotIn('rexruntime.pdb',rewritten)
                self.assertTrue((root/'runtime/xbox360/espgaluda2_crash_writer.exe').is_file())
                self.assertTrue((root/'runtime/xbox360/futari_crash_writer.exe').is_file())
                self.assertEqual(certificate.read_bytes(),first_certificate)
                self.assertEqual(result['stagedFiles'],7)
                self.assertFalse(result['gameDataIncluded'])

    def test_corrupted_source_is_rejected_before_existing_manifest_changes(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);build=root/'build';build.mkdir()
            with patch.object(stage_game,'ROOT',root):
                game,receipt=self.fixture(root,'futari',build)
                stage_game.stage_one(game,receipt,build)
                manifest=root/'runtime/games/futari/runtime-manifest.json'
                original=manifest.read_bytes()
                (build/'futari.exe').write_bytes(b'changed authored fixture')
                with self.assertRaises(ValueError):stage_game.stage_one(game,receipt,build)
                self.assertEqual(manifest.read_bytes(),original)


if __name__=='__main__':unittest.main()
