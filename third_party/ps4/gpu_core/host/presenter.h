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
            Vulkan::Scheduler &, Vulkan::Runtime &, VideoCore::TextureCache &, bool vsync = true,
            bool shared_present_queue = false);
  ~Presenter();
  RuntimePresentResult Present(const RuntimeDisplay &);
  RuntimePresentResult Prepare(const RuntimeDisplay &);
  RuntimePresentResult Complete(bool accepted);
  void ReadPreparedSnapshot(void* output, std::uint64_t bytes);
  RuntimePresentationStats Stats();
  void PauseForDiagnostics(bool paused);
  void Drain();
};
} // namespace Ketsui::GpuCore
