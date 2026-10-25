"""Source contracts for SDK-owned community cards; no ROM or UI execution."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GamerCardContracts(unittest.TestCase):
    def test_ascii_labels_and_keyboard_opt_in(self):
        text = (ROOT / 'third_party/rexglue/src/kernel/xam/xam_ui.cpp').read_text()
        self.assertIn('"PLAYER CARD",name,{"CLOSE"},0,true', text)
        self.assertIn('bool keyboard_close = false', text)
        self.assertIn('if (keyboard_close_ &&', text)
        self.assertIn('ImGui::IsKeyReleased(ImGuiKey_Enter)', text)
        self.assertIn('ImGui::IsKeyReleased(ImGuiKey_Escape)', text)

    def test_cancelled_guide_not_exposed(self):
        self.assertNotIn('keyboard_guide', (ROOT / 'common/settings.h').read_text())
        self.assertNotIn('guide =', (ROOT / 'config/default.ini').read_text())
        self.assertNotIn('guide_dialog', (ROOT / 'backends/xbox360/host.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
