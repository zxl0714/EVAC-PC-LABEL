#pragma once
#include "video_core/amdgpu/cb_db_extent.h"
#include "video_core/amdgpu/regs.h"
#include <thread>

namespace Vulkan { class Rasterizer; }
namespace AmdGpu {
// Register state only; submission and host services are supplied separately.
struct Liverpool {
    Regs regs{};
    u32 index_buffer_num_indices{};
    std::array<CbDbExtent, NUM_COLOR_BUFFERS> last_cb_extent{};
    CbDbExtent last_db_extent{};
    ComputeProgram compute{};
    Vulkan::Rasterizer* rasterizer{};
    std::thread::id owner{std::this_thread::get_id()};
    void BindRasterizer(Vulkan::Rasterizer* value) { rasterizer = value; }
    ComputeProgram& GetCsRegs() { return compute; }
    std::thread::id GetGpuCommandProcessorThread() const { return owner; }
};
}
