// Opt-in test/accessibility adapter for genuine window key events whose down
// and up arrive in one UI batch. The normal portable launcher does not enable it.
#include <rex/runtime.h>
#include <rex/cvar.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/imgui_drawer.h>
#include <array>
#include <chrono>
#include <cstdlib>
#include <mutex>

namespace {
using rex::X_STATUS;
using rex::X_RESULT;
class KeyboardTapTest final : public rex::input::InputDriver, public rex::ui::WindowInputListener {
 public:
  explicit KeyboardTapTest(rex::ui::Window* window) : InputDriver(window, 1) {
    window->AddInputListener(this, 1);
  }
  ~KeyboardTapTest() override { window()->RemoveInputListener(this); }
  X_STATUS Setup() override { return 0; }
  bool IsKeyboardEmulation() const override { return true; }
  X_RESULT GetCapabilities(uint32_t, uint32_t, rex::input::X_INPUT_CAPABILITIES*) override { return 1167; }
  X_RESULT SetState(uint32_t, rex::input::X_INPUT_VIBRATION*) override { return 1167; }
  X_RESULT GetKeystroke(uint32_t, uint32_t, rex::input::X_INPUT_KEYSTROKE*) override { return 1167; }
  X_RESULT GetState(uint32_t user, rex::input::X_INPUT_STATE* state) override {
    if (!state || !window()->HasFocus() || !is_active() ||
        rex::cvar::GetFlagByName("mnk_mode") != "true" ||
        user != std::stoul(rex::cvar::GetFlagByName("mnk_user_index"))) return 1167;
    uint16_t buttons = 0;
    const auto now = std::chrono::steady_clock::now();
    {
      std::lock_guard lock(mutex_);
      for (size_t bit = 0; bit < expires_.size(); ++bit) if (expires_[bit] > now) buttons |= uint16_t(1u << bit);
    }
    if (!buttons) return 1167;
    *state = {};
    state->packet_number = ++packet_;
    state->gamepad.buttons = buttons;
    return 0;
  }
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.prev_state() || e.is_handled() || e.is_alt_pressed() || e.is_ctrl_pressed() || !window()->HasFocus()) return;
    static constexpr std::pair<const char*, uint16_t> mappings[] = {
      {"keybind_dpad_up", 1}, {"keybind_dpad_down", 2}, {"keybind_dpad_left", 4}, {"keybind_dpad_right", 8},
      {"keybind_start", 0x10}, {"keybind_back", 0x20}, {"keybind_left_shoulder", 0x100}, {"keybind_right_shoulder", 0x200},
      {"keybind_a", 0x1000}, {"keybind_b", 0x2000}, {"keybind_x", 0x4000}, {"keybind_y", 0x8000}};
    uint16_t mask = 0;
    for (const auto& [name, button] : mappings) {
      if (rex::ui::ParseVirtualKey(rex::cvar::GetFlagByName(name)) == e.virtual_key()) mask |= button;
    }
    // Optional bounded accessibility hold for a genuine F11 window key event.
    // The game's actual SHOT is A, not X (Player Change). Space/menu confirms
    // keep 40ms; only the explicit test-only F11 alias holds A, at most 5 seconds.
    // No key or automatic gameplay is generated; normal launch has no adapter.
    int duration=40;
    const char* hold=std::getenv("FUTARI_KEYBOARD_SHOT_HOLD_TEST_MS");
    if(hold && e.virtual_key()==rex::ui::ParseVirtualKey("F11")){
      const int requested=std::atoi(hold);
      if(requested>=40 && requested<=5000){mask|=0x1000;duration=requested;}
    }
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration);
    std::lock_guard lock(mutex_);
    for (size_t bit = 0; bit < expires_.size(); ++bit) if (mask & (1u << bit)) expires_[bit] = until;
  }
 private:
  std::mutex mutex_;
  std::array<std::chrono::steady_clock::time_point, 16> expires_{};
  uint32_t packet_ = 0;
};
}

void FutariAttachKeyboardTapTest(rex::ui::Window* window, rex::Runtime* runtime) {
  const char* flag = std::getenv("FUTARI_KEYBOARD_TAP_TEST");
  if (!flag || std::string(flag) != "1") return;
  auto* input = dynamic_cast<rex::input::InputSystem*>(runtime->input_system());
  if (!input) return;
  // The UI automation keyboard surface emits non-extended arrow scancodes,
  // observed by SDL as these numpad keys. Only alter the opt-in test session.
  rex::cvar::SetFlagByName("keybind_dpad_down", "Numpad2");
  rex::cvar::SetFlagByName("keybind_dpad_up", "Numpad8");
  rex::cvar::SetFlagByName("keybind_dpad_left", "Numpad4");
  rex::cvar::SetFlagByName("keybind_dpad_right", "Numpad6");
  rex::cvar::SetFlagByName("keybind_start", "Return");
  auto driver = std::make_unique<KeyboardTapTest>(window);
  driver->set_is_active_callback([runtime] {
    return !runtime->imgui_drawer() || !runtime->imgui_drawer()->HasInteractiveDialogs();
  });
  driver->Setup();
  input->AddDriver(std::move(driver));
  REXLOG_INFO("Futari test keyboard adapter enabled: real key events, 40ms minimum tap; disabled in normal launch");
}
