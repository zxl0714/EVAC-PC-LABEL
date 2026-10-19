#pragma once

#include "common/types.h"

namespace Frontend {

enum class WindowSystemType : u8 { Headless, Windows, X11, Wayland, Metal };

struct WindowSystemInfo {
  void *display_connection = nullptr;
  void *render_surface = nullptr;
  float render_surface_scale = 1.0f;
  WindowSystemType type = WindowSystemType::Headless;
};

// Compatibility type for the GPU source only; owns no SDL or native window.
class WindowSDL {
public:
  explicit WindowSDL(WindowSystemInfo info = {}) : info{info} {}
  WindowSystemInfo GetWindowInfo() const { return info; }

private:
  WindowSystemInfo info;
};

} // namespace Frontend
