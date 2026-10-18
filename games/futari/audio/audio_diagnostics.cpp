#include "audio_diagnostics.h"
#include <rex/logging.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace {
class AudioMarker final : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& event) override {
    if (uint32_t(event.virtual_key()) != 0x77 || event.prev_state()) return; // F8
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    REXLOG_INFO("AUDIO_DIAG marker t_us={} sequence={} source=F8 meaning=user_observation", now, ++sequence_);
  }
 private:
  uint32_t sequence_ = 0;
};
}
std::unique_ptr<rex::ui::WindowInputListener> FutariCreateAudioMarker(rex::ui::Window* window) {
  const char* flag = std::getenv("FUTARI_AUDIO_DIAGNOSTICS");
  if (flag && std::strcmp(flag, "0") == 0) return nullptr;
  auto listener = std::make_unique<AudioMarker>();
  window->AddInputListener(listener.get(), 100000);
  REXLOG_INFO("AUDIO_DIAG marker_ready key=F8; mark before death and after audible delay; no recording/upload");
  return listener;
}
