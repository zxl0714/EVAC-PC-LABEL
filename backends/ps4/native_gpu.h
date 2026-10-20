#pragma once
#include "native_video.h"
#include "graphics/pm4.h"
#include "graphics/shader_compiler.h"
#include <vector>

// The native adapter owns guest memory validation; KetsuiGraphics receives only
// typed state and bounded CPU snapshots, never an arbitrary game pointer.
struct NativeGpuExecution {
    unsigned draws{}, acquires{}, prefetches{};
    bool prepared_flip{};
    // Aggregated host wall time on the resident worker, not GPU timestamps.
    // The remaining execute_us includes the worker handoff and adapter scope.
    std::uint64_t duration_us{}, draw_us{}, acquire_us{}, prefetch_us{}, flush_us{}, other_us{};
};
NativeGpuExecution ExecuteNativeGpuCommands(NativeVideoGpuContext& target,
    std::span<const std::vector<std::uint32_t>> streams, kt::gfx::pm4::Analysis& state,
    kt::gfx::ShaderMemoryReader reader);
bool GpuAdapterSelfTest(kt::gfx::VulkanDevice& device);
// Set once before guest threads start. Full mode is the diagnostic A/B path.
void ConfigureNativeTextureTracking(bool enabled);
void ConfigureNativeAttachmentReuse(bool enabled);
