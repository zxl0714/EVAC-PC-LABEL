"""SDL virtual controllers cover physical-input contracts without real devices."""
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
EXE=ROOT/'out/native/KetsuiNativeProbe.exe'


class GamepadTests(unittest.TestCase):
    def invoke(self,*args,exe=EXE):
        return subprocess.run([str(exe),*args],capture_output=True,text=True,encoding='utf-8',timeout=20)

    def test_virtual_buttons_axes_edges_focus_and_disconnect(self):
        result=self.invoke('--gamepad-self-test')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        cases=[event for line in result.stdout.splitlines() if (event:=json.loads(line))['event']=='sdl_gamepad_self_test_case']
        self.assertGreater(len(cases),25)
        self.assertTrue(all(case['passed'] for case in cases),result.stdout)
        names={case['name'] for case in cases}
        self.assertTrue({'short_down_up_retained_until_game_acknowledges',
            'old_acknowledgement_cannot_erase_new_edge','focus_loss_clears_buttons_and_pending',
            'background_short_press_not_replayed_on_refocus','disconnect_neutralizes_and_drops_pending'}<=names)

    def test_monitor_duration_is_bounded_and_validated(self):
        for value in ('-1','181','9999','abc','1.5'):
            with self.subTest(value=value):
                result=self.invoke('--gamepad-monitor',value)
                self.assertEqual(result.returncode,2,result.stdout+result.stderr)
                self.assertNotIn('native_sdl_gamepad_initialized',result.stdout)

    def test_device_list_has_no_game_entry_or_window(self):
        result=self.invoke('--gamepad-list')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        events=[json.loads(line) for line in result.stdout.splitlines()]
        initialized=next(e for e in events if e['event']=='native_sdl_gamepad_initialized')
        self.assertFalse(initialized['owns_window'])
        self.assertFalse(initialized['owns_audio'])
        self.assertFalse(any(e['event']=='enter_game' for e in events))
        self.assertEqual(events[-1]['event'],'native_sdl_gamepad_monitor_complete')

    def test_adjacent_mapping_file_unicode_comments_and_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'中文 映射'
            root.mkdir()
            exe=root/EXE.name
            shutil.copy2(EXE,exe)
            mapping=root/'gamecontrollerdb.txt'
            mapping.write_text('\ufeff# optional mappings\n; comment\n',encoding='utf-8')
            result=self.invoke('--gamepad-list',exe=exe)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('"entries":0',result.stdout)
            mapping.write_text('not a mapping\n',encoding='utf-8')
            result=self.invoke('--gamepad-list',exe=exe)
            self.assertEqual(result.returncode,2,result.stdout+result.stderr)
            self.assertIn('Gamepad mapping line 1',result.stderr)


if __name__=='__main__':
    unittest.main()
