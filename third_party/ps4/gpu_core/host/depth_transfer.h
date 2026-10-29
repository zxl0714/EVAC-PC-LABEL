// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/renderer_vulkan/vk_common.h"
#include "common/types.h"
namespace VideoCore { struct Buffer; }
namespace Vulkan {
class Instance;
class Runtime;
class Scheduler;
// Vulkan depth-aspect transfers use 32-bit texels for D24S8, while a guest
// D16S8 resource stores depth separately as 16-bit texels. Convert on the GPU.
class DepthTransfer {
public:
    DepthTransfer(const Instance& instance, Scheduler& scheduler, Runtime& runtime);
    ~DepthTransfer();
    void Convert(const VideoCore::Buffer* src, u64 src_offset,
                 const VideoCore::Buffer* dst, u64 dst_offset, u32 pixels, bool compress,
                 bool float_depth = false);
private:
    const Instance& instance;
    Scheduler& scheduler;
    Runtime& runtime;
    vk::UniqueDescriptorSetLayout descriptors;
    vk::UniquePipelineLayout layout;
    vk::UniquePipeline pipeline;
};
}
