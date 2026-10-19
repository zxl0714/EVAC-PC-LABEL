#pragma once
#include <cstdint>
#include <functional>
namespace Vulkan {
class Instance;
}
namespace Ketsui::GpuCore {
class NativeMemoryCacheBackend;
struct RecompilerResults {
  std::uint32_t checks{}, modules{};
  std::uint64_t spirv_words{};
};
RecompilerResults TestRecompiler(
    const Vulkan::Instance &, NativeMemoryCacheBackend &,
    unsigned char *cpu_scratch, std::uint64_t gpu_scratch,
    const std::function<void(std::uint64_t, std::uint32_t)> &gpu_fill);
} // namespace Ketsui::GpuCore
