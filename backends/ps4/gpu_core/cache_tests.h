#pragma once
#include <cstdint>
namespace Vulkan {
class Instance;
}
namespace Ketsui::GpuCore {
struct CacheResults {
  std::uint32_t checks{}, read_faults{}, write_faults{};
  std::uint64_t verified_bytes{};
  std::uint32_t recompiler_checks{}, recompiler_modules{};
  std::uint64_t recompiler_spirv_words{};
};
CacheResults TestCaches(const Vulkan::Instance &instance);
} // namespace Ketsui::GpuCore
