#pragma once
#include "display_layout.h"
#include "raster.h"
#include <cstdint>
#include <memory>

namespace kt::gfx {
enum class PresentStatus { Completed, ZeroExtent, OutOfDate };
// Return UINT32_MAX when no compatible type exists. Preference never weakens
// required flags or the resource's memoryTypeBits mask (at most 32 types).
std::uint32_t SelectMemoryType(std::span<const std::uint32_t> flags, std::uint32_t bits,
    std::uint32_t required, std::uint32_t preferred = 0) noexcept;
// No ELF, SCE kernel, emulator settings or game memory dependencies belong in
// this component. The caller owns the HWND and keeps it alive through teardown.
class VulkanDevice {
public:
    explicit VulkanDevice(void* native_window);
    ~VulkanDevice();
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    std::uint64_t CreateDisplayImage(const DisplayLayout& layout);
    void DestroyDisplayImage(std::uint64_t image);
    std::size_t ImageCount() const;
    void WaitIdle();
    // Synchronous correctness path: all inputs are owned/pinned by the caller;
    // outputs become CPU-visible only after the submission fence signals.
    RasterResult Draw(const RasterDraw& draw);
    RasterCacheStats CacheStats() const;
    // Drains GPU work before retiring persistent sampled images and pipelines.
    void TrimRasterCaches();
    // Upload raw display pixels and complete one real WSI presentation. A
    // maintenance1 present fence, not a vblank timer, owns semaphore lifetime.
    PresentStatus Present(std::uint64_t image, std::span<const std::uint8_t> linear);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace kt::gfx
