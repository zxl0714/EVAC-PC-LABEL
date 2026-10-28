import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from futari_keyboard_frontend import frontend

KEY='INPUT_DIAG key=102 down=true repeat=false before=0000 after=0008 polls=20 t_us=100 frame=8'
UP='INPUT_DIAG key=102 down=false repeat=false before=0008 after=0000 polls=20 t_us=105 frame=8'
POLL='[t30] INPUT_DIAG poll count=21 active=true buttons=0008 t_us=110 frame=9'


class KeyboardFrontEndTests(unittest.TestCase):
    def test_short_press_is_paired_to_exact_first_original_poll(self):
        pair,=frontend('\n'.join((KEY,UP,POLL)))
        self.assertEqual(pair['hold_us'],5)
        self.assertEqual(pair['poll']['count'],21)
        self.assertEqual(pair['poll']['us']-pair['key']['us'],10)

    def test_later_same_button_snapshot_does_not_replace_missing_first_poll(self):
        pair,=frontend('\n'.join((KEY,UP,POLL.replace('count=21','count=22'))))
        self.assertIsNone(pair['poll'])

    def test_wrong_mask_and_inactive_snapshot_are_not_received_input(self):
        for poll in (POLL.replace('buttons=0008','buttons=0004'),POLL.replace('active=true','active=false')):
            pair,=frontend('\n'.join((KEY,UP,poll)))
            self.assertIsNone(pair['poll'])

    def test_unbound_key_is_not_a_direction_source(self):
        self.assertEqual(frontend(KEY.replace('after=0008','after=0000')),[])


if __name__=='__main__':unittest.main()
