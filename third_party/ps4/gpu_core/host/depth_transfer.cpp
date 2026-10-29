// SPDX-License-Identifier: GPL-2.0-or-later
#include "depth_transfer.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/host_shaders/depth_transfer_comp.h"
#include <array>
#include <stdexcept>

namespace Vulkan {
DepthTransfer::DepthTransfer(const Instance& instance_,Scheduler& scheduler_,Runtime& runtime_)
    :instance(instance_),scheduler(scheduler_),runtime(runtime_) {
    const auto device=instance.GetDevice();
    std::array<vk::DescriptorSetLayoutBinding,2> bindings{};
    for(u32 i=0;i<2;++i) bindings[i]={i,vk::DescriptorType::eStorageBuffer,1,vk::ShaderStageFlagBits::eCompute};
    const vk::DescriptorSetLayoutCreateInfo desc_ci{
        .flags=vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount=2,.pBindings=bindings.data()};
    auto desc=device.createDescriptorSetLayoutUnique(desc_ci);
    if(desc.result!=vk::Result::eSuccess) throw std::runtime_error("Depth transfer descriptor creation failed");
    descriptors=std::move(desc.value);
    const vk::DescriptorSetLayout set=*descriptors;
    const vk::PushConstantRange constants{vk::ShaderStageFlagBits::eCompute,0,16};
    const vk::PipelineLayoutCreateInfo layout_ci{
        .setLayoutCount=1,.pSetLayouts=&set,.pushConstantRangeCount=1,.pPushConstantRanges=&constants};
    auto pl=device.createPipelineLayoutUnique(layout_ci);
    if(pl.result!=vk::Result::eSuccess) throw std::runtime_error("Depth transfer layout creation failed");
    layout=std::move(pl.value);
    const auto module=CompileSPV(DEPTH_TRANSFER_COMP,device);
    const vk::ComputePipelineCreateInfo pipeline_ci{
        .stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=module,.pName="main"},.layout=*layout};
    auto result=device.createComputePipelineUnique({},pipeline_ci);
    device.destroyShaderModule(module);
    if(result.result!=vk::Result::eSuccess) throw std::runtime_error("Depth transfer pipeline creation failed");
    pipeline=std::move(result.value);
}
DepthTransfer::~DepthTransfer()=default;
void DepthTransfer::Convert(const VideoCore::Buffer* src,u64 src_offset,
                            const VideoCore::Buffer* dst,u64 dst_offset,u32 pixels,bool compress,bool float_depth) {
    if(!pixels || (src_offset&3) || (dst_offset&3)) throw std::runtime_error("Unaligned depth transfer");
    const u64 src_bytes=(u64(pixels)*(compress?4u:2u)+3)&~3ull;
    const u64 dst_bytes=(u64(pixels)*(compress?2u:4u)+3)&~3ull;
    if(src_offset>src->SizeBytes() || src_bytes>src->SizeBytes()-src_offset ||
       dst_offset>dst->SizeBytes() || dst_bytes>dst->SizeBytes()-dst_offset)
        throw std::runtime_error("Depth transfer exceeds buffer extent");
    scheduler.EndRendering();
    const bool src_barrier=runtime.IsBufferAccessed(src,src_offset,src_bytes);
    const bool dst_barrier=runtime.IsBufferAccessed(dst,dst_offset,dst_bytes,true);
    if(src_barrier || dst_barrier) runtime.FlushBarriers();
    const u64 alignment=instance.StorageMinAlignment();
    const u64 src_base=src_offset/alignment*alignment,dst_base=dst_offset/alignment*alignment;
    const std::array<vk::DescriptorBufferInfo,2> info{{
        {src->Handle(),src_base,src_offset-src_base+src_bytes},
        {dst->Handle(),dst_base,dst_offset-dst_base+dst_bytes}}};
    std::array<vk::WriteDescriptorSet,2> writes{};
    for(u32 i=0;i<2;++i) writes[i]={.dstBinding=i,.descriptorCount=1,
        .descriptorType=vk::DescriptorType::eStorageBuffer,.pBufferInfo=&info[i]};
    const auto cmd=scheduler.CommandBuffer();
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline);
    cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,*layout,0,writes);
    const std::array<u32,4> params{u32((src_offset-src_base)/4),u32((dst_offset-dst_base)/4),pixels,u32(compress)|(u32(float_depth)<<1)};
    cmd.pushConstants(*layout,vk::ShaderStageFlagBits::eCompute,0,16,params.data());
    cmd.dispatch((pixels+127u)/128u,1,1);
    runtime.AccessBuffer(src,src_offset,src_bytes,vk::PipelineStageFlagBits2::eComputeShader,vk::AccessFlagBits2::eShaderRead);
    runtime.AccessBuffer(dst,dst_offset,dst_bytes,vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite);
}
}
