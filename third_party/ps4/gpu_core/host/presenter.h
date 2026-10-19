#pragma once
#include "runtime_api.h"
#include <memory>
namespace Frontend {
class WindowSDL;
}
namespace Vulkan {
class Instance;
class Scheduler;
class Runtime;
} // namespace Vulkan
namespace VideoCore {
class TextureCache;
}
namespace Ketsui::GpuCore {
class Presenter {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  Presenter(const Vulkan::Instance &, const Frontend::WindowSDL &,
            Vulkan::Scheduler &, Vulkan::Runtime &, VideoCore::TextureCache &, bool vsync = true);
  ~Presenter();
  RuntimePresentResult Present(const RuntimeDisplay &);
  void Drain();
};
} // namespace Ketsui::GpuCore
