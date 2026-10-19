#pragma once

#include <cstdint>

namespace Vulkan {
class Instance;
}
namespace Ketsui::GpuCore {
struct BufferPoolResults {
  std::uint32_t checks{};
  std::uint64_t verified_bytes{};
};
BufferPoolResults TestBufferPools(const Vulkan::Instance &instance);
} // namespace Ketsui::GpuCore
