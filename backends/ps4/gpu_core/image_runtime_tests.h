#pragma once
#include <cstdint>
namespace Vulkan {
class Instance;
}
namespace Ketsui::GpuCore {
struct ImageRuntimeResults {
  std::uint32_t checks{};
  std::uint64_t verified_bytes{};
};
ImageRuntimeResults TestImageRuntime(const Vulkan::Instance &instance);
} // namespace Ketsui::GpuCore
