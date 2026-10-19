// SPDX-License-Identifier: GPL-2.0-or-later
#include "presenter.h"
#include "graphics/present_mode.h"
#include "diagnostics/monotonic_clock.h"
#include "cache_memory.h"
#include "core/libraries/videoout/buffer.h"
#include "sdl_window.h"
#include "video_core/host_shaders/fs_tri_vert.h"
#include "video_core/host_shaders/host_present_frag.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace Ketsui::GpuCore {
namespace {
void Require(bool ok, const char *text) {
  if (!ok) {
    throw std::runtime_error(text);
  }
}
void Check(vk::Result result, const char *text) {
  if (result != vk::Result::eSuccess) {
    throw std::runtime_error(std::string{text} + ": " + vk::to_string(result));
  }
}
template <typename T> T Checked(vk::ResultValue<T> value, const char *text) {
  Check(value.result, text);
  return std::move(value.value);
}
struct Surface {
  vk::Instance instance;
  vk::SurfaceKHR value;
  Surface(vk::Instance instance_, const Frontend::WindowSDL &window)
      : instance{instance_}, value{Vulkan::CreateSurface(instance_, window)} {}
  ~Surface() { instance.destroySurfaceKHR(value); }
};
} // namespace
struct Presenter::Impl {
  const Vulkan::Instance &instance;
  HWND window;
  Vulkan::Scheduler &scheduler;
  Vulkan::Runtime &runtime;
  VideoCore::TextureCache &cache;
  Surface surface;
  vk::UniqueSwapchainKHR chain;
  vk::Extent2D extent{};
  vk::Format format{};
  std::vector<vk::Image> images;
  std::vector<vk::UniqueImageView> views;
  std::vector<bool> initialized;
  vk::UniqueDescriptorSetLayout descriptors;
  vk::UniquePipelineLayout layout;
  vk::UniqueSampler sampler;
  vk::UniquePipeline pipeline;
  vk::UniqueFence acquired, completed, presented;
  vk::UniqueSemaphore ready;
  bool gpu_pending{}, present_pending{};
  bool vsync = true;
  u64 presents{};
  Impl(const Vulkan::Instance &instance_, const Frontend::WindowSDL &window_,
       Vulkan::Scheduler &scheduler_, Vulkan::Runtime &runtime_,
       VideoCore::TextureCache &cache_, bool vsync_)
      : instance{instance_},
        window{static_cast<HWND>(window_.GetWindowInfo().render_surface)},
        scheduler{scheduler_}, runtime{runtime_}, cache{cache_},
        surface{instance.GetInstance(), window_}, vsync{vsync_} {
    Require(window && IsWindow(window) && instance.HasPresentFences(),
            "Presenter needs a live window and present fences");
    Require(Checked(instance.GetPhysicalDevice().getSurfaceSupportKHR(
                        instance.GetGraphicsQueueFamilyIndex(), surface.value),
                    "Query present queue"),
            "Graphics queue cannot present to the window");
    const auto device = instance.GetDevice();
    acquired = Checked(device.createFenceUnique({}), "Create acquire fence");
    completed = Checked(device.createFenceUnique({}), "Create submit fence");
    presented =
        Checked(device.createFenceUnique({}), "Create presentation fence");
    ready =
        Checked(device.createSemaphoreUnique({}), "Create present semaphore");
    const vk::DescriptorSetLayoutBinding binding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment};
    descriptors = Checked(
        device.createDescriptorSetLayoutUnique(
            {.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
             .bindingCount = 1,
             .pBindings = &binding}),
        "Create presentation descriptors");
    const auto descriptor_layout = *descriptors;
    layout =
        Checked(device.createPipelineLayoutUnique(
                    {.setLayoutCount = 1, .pSetLayouts = &descriptor_layout}),
                "Create presentation layout");
    sampler = Checked(device.createSamplerUnique(
                          {.magFilter = vk::Filter::eNearest,
                           .minFilter = vk::Filter::eNearest,
                           .mipmapMode = vk::SamplerMipmapMode::eNearest,
                           .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                           .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                           .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                           .maxLod = 0}),
                      "Create presentation sampler");
  }
  ~Impl() {
    try {
      Drain();
    } catch (const std::exception &error) {
      std::fprintf(stderr, "Presentation teardown failed: %s\n", error.what());
      std::terminate();
    }
    views.clear();
    chain.reset();
  }
  void Drain() {
    const auto device = instance.GetDevice();
    if (gpu_pending) {
      Check(device.waitForFences(*completed, true, UINT64_MAX),
            "Drain presentation submit");
      gpu_pending = false;
    }
    if (present_pending) {
      Check(device.waitForFences(*presented, true, UINT64_MAX),
            "Drain presentation engine");
      present_pending = false;
    }
  }
  void CreatePipeline() {
    const auto device = instance.GetDevice();
    const auto vs =
        Checked(device.createShaderModuleUnique(
                    {.codeSize = sizeof(FS_TRI_VERT), .pCode = FS_TRI_VERT}),
                "Create presentation VS");
    const auto fs = Checked(
        device.createShaderModuleUnique({.codeSize = sizeof(HOST_PRESENT_FRAG),
                                         .pCode = HOST_PRESENT_FRAG}),
        "Create presentation FS");
    const std::array stages{vk::PipelineShaderStageCreateInfo{
                                .stage = vk::ShaderStageFlagBits::eVertex,
                                .module = *vs,
                                .pName = "main"},
                            vk::PipelineShaderStageCreateInfo{
                                .stage = vk::ShaderStageFlagBits::eFragment,
                                .module = *fs,
                                .pName = "main"}};
    const vk::PipelineVertexInputStateCreateInfo vertex{};
    const vk::PipelineInputAssemblyStateCreateInfo assembly{
        .topology = vk::PrimitiveTopology::eTriangleList};
    const vk::PipelineViewportStateCreateInfo viewport{.viewportCount = 1,
                                                       .scissorCount = 1};
    const vk::PipelineRasterizationStateCreateInfo raster{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1};
    const vk::PipelineMultisampleStateCreateInfo samples{
        .rasterizationSamples = vk::SampleCountFlagBits::e1};
    const vk::PipelineColorBlendAttachmentState color{
        .colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
    const vk::PipelineColorBlendStateCreateInfo blend{.attachmentCount = 1,
                                                      .pAttachments = &color};
    const std::array dynamics{vk::DynamicState::eViewport,
                              vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic{
        .dynamicStateCount = static_cast<u32>(dynamics.size()),
        .pDynamicStates = dynamics.data()};
    const vk::PipelineRenderingCreateInfo rendering{
        .colorAttachmentCount = 1, .pColorAttachmentFormats = &format};
    pipeline = Checked(device.createGraphicsPipelineUnique(
                           {}, {.pNext = &rendering,
                                .stageCount = static_cast<u32>(stages.size()),
                                .pStages = stages.data(),
                                .pVertexInputState = &vertex,
                                .pInputAssemblyState = &assembly,
                                .pViewportState = &viewport,
                                .pRasterizationState = &raster,
                                .pMultisampleState = &samples,
                                .pColorBlendState = &blend,
                                .pDynamicState = &dynamic,
                                .layout = *layout}),
                       "Create presentation pipeline");
  }
  void RetireChain() {
    Drain();
    views.clear();
    chain.reset();
    images.clear();
    initialized.clear();
  }
  vk::PresentModeKHR SelectMode() const {
    const auto modes = Checked(instance.GetPhysicalDevice().getSurfacePresentModesKHR(surface.value), "Query present modes");
    const auto selected = cave::ps4::SelectPresentMode(vsync, std::span<const vk::PresentModeKHR>(modes),
        vk::PresentModeKHR::eFifo, vk::PresentModeKHR::eImmediate, vk::PresentModeKHR::eMailbox);
    std::fprintf(stderr, "gpu_runtime_swapchain: vsync_requested=%s mode=%s\n", vsync ? "true" : "false", vk::to_string(selected).c_str());
    return selected;
  }
  void CreateChain(vk::Extent2D size, const vk::SurfaceCapabilitiesKHR &caps) {
    RetireChain();
    const auto formats = Checked(
        instance.GetPhysicalDevice().getSurfaceFormatsKHR(surface.value),
        "Query window formats");
    const auto selected = std::ranges::find_if(formats, [](const auto &value) {
      return (value.format == vk::Format::eB8G8R8A8Srgb ||
              value.format == vk::Format::eR8G8B8A8Srgb) &&
             value.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    Require(selected != formats.end(), "No SDR sRGB presentation format");
    Require(bool(caps.supportedUsageFlags &
                 vk::ImageUsageFlagBits::eColorAttachment),
            "Surface cannot be a color attachment");
    vk::CompositeAlphaFlagBitsKHR alpha{};
    for (auto value : {vk::CompositeAlphaFlagBitsKHR::eOpaque,
                       vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                       vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
                       vk::CompositeAlphaFlagBitsKHR::eInherit}) {
      if (caps.supportedCompositeAlpha & value) {
        alpha = value;
        break;
      }
    }
    u32 count = caps.minImageCount + 1;
    if (caps.maxImageCount) {
      count = std::min(count, caps.maxImageCount);
    }
    const auto device = instance.GetDevice();
    chain = Checked(device.createSwapchainKHRUnique(
                        {.surface = surface.value,
                         .minImageCount = count,
                         .imageFormat = selected->format,
                         .imageColorSpace = selected->colorSpace,
                         .imageExtent = size,
                         .imageArrayLayers = 1,
                         .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
                         .imageSharingMode = vk::SharingMode::eExclusive,
                         .preTransform = caps.currentTransform,
                         .compositeAlpha = alpha,
                         .presentMode = SelectMode(),
                         .clipped = true}),
                    "Create tracked swapchain");
    images =
        Checked(device.getSwapchainImagesKHR(*chain), "Get swapchain images");
    initialized.assign(images.size(), false);
    for (auto image : images) {
      views.push_back(
          Checked(device.createImageViewUnique(
                      {.image = image,
                       .viewType = vk::ImageViewType::e2D,
                       .format = selected->format,
                       .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0,
                                            1, 0, 1}}),
                  "Create swapchain view"));
    }
    extent = size;
    if (!pipeline || format != selected->format) {
      format = selected->format;
      CreatePipeline();
    }
  }
  RuntimePresentResult Present(const RuntimeDisplay &display) {
    const auto began = kt::diagnostics::MonotonicMicroseconds();
    Require(display.width && display.height && display.width <= 16384 &&
                display.height <= 16384 && display.tiling_mode <= 1 &&
                (display.pixel_format == 0x80000000 ||
                 display.pixel_format == 0x80002200) &&
                display.address &&
                display.address % (display.tiling_mode ? 256 : 32768) == 0,
            "Invalid display description");
    Libraries::VideoOut::BufferAttributeGroup group{};
    group.is_occupied = true;
    group.attrib = {
        .pixel_format =
            static_cast<Libraries::VideoOut::PixelFormat>(display.pixel_format),
        .tiling_mode =
            static_cast<Libraries::VideoOut::TilingMode>(display.tiling_mode),
        .width = display.width,
        .height = display.height,
        .pitch_in_pixel = display.pitch};
    VideoCore::TextureCache::ImageDesc desc{group, display.address};
    Require(display.pitch == desc.info.pitch &&
                CacheMemory().IsMapped(display.address, desc.info.guest_size),
            "Display pitch or backing is invalid");
    RuntimePresentResult result{.status = RuntimePresentStatus::ZeroExtent,
                                .completed_presents = presents};
    RECT client{};
    Require(IsWindow(window) && GetClientRect(window, &client),
            "Presentation window no longer exists");
    if (IsIconic(window) || client.right <= 0 || client.bottom <= 0) {
      return result;
    }
    const auto caps = Checked(
        instance.GetPhysicalDevice().getSurfaceCapabilitiesKHR(surface.value),
        "Query window capabilities");
    auto size = caps.currentExtent;
    if (size.width == UINT32_MAX) {
      size = {std::clamp(static_cast<u32>(client.right),
                         caps.minImageExtent.width, caps.maxImageExtent.width),
              std::clamp(static_cast<u32>(client.bottom),
                         caps.minImageExtent.height,
                         caps.maxImageExtent.height)};
    }
    if (!size.width || !size.height) {
      return result;
    }
    result.width = size.width;
    result.height = size.height;
    if (!chain || extent != size) {
      CreateChain(size, caps);
    }
    const auto device = instance.GetDevice();
    const auto prepared_at = kt::diagnostics::MonotonicMicroseconds();
    Drain();
    Check(device.resetFences({*acquired, *completed, *presented}),
          "Reset presentation fences");
    const auto retired_at = kt::diagnostics::MonotonicMicroseconds();
    u32 index{};
    const auto acquire = device.acquireNextImageKHR(*chain, 1'000'000'000ull,
                                                    {}, *acquired, &index);
    if (acquire == vk::Result::eErrorOutOfDateKHR) {
      RetireChain();
      result.status = RuntimePresentStatus::OutOfDate;
      return result;
    }
    Check(acquire == vk::Result::eSuboptimalKHR ? vk::Result::eSuccess
                                                : acquire,
          "Acquire swapchain image");
    Check(device.waitForFences(*acquired, true, 5'000'000'000ull),
          "Wait swapchain acquire");
    const auto acquired_at = kt::diagnostics::MonotonicMicroseconds();
    Require(index < images.size(), "Acquired swapchain index is invalid");
    const auto image_id = cache.FindImage(desc);
    cache.UpdateImage(image_id);
    auto &image = cache.GetImage(image_id);
    VideoCore::ImageViewInfo view_info{};
    view_info.format = display.pixel_format == 0x80000000
                           ? vk::Format::eB8G8R8A8Srgb
                           : vk::Format::eR8G8B8A8Srgb;
    view_info.mapping.a = AmdGpu::CompSwizzle::One;
    const auto source_view = *image.FindView(view_info).image_view;
    scheduler.EndRendering();
    runtime.Transit(&image, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::PipelineStageFlagBits2::eFragmentShader,
                    vk::AccessFlagBits2::eShaderRead);
    runtime.FlushBarriers();
    const auto command = scheduler.CommandBuffer();
    const vk::ImageMemoryBarrier2 before{
        .srcStageMask = vk::PipelineStageFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = initialized[index] ? vk::ImageLayout::ePresentSrcKHR
                                        : vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = images[index],
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    command.pipelineBarrier2(
        {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &before});
    const vk::RenderingAttachmentInfo attachment{
        .imageView = *views[index],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = {.color = {.float32 = std::array<float, 4>{0, 0, 0, 1}}}};
    command.beginRendering({.renderArea = {{0, 0}, extent},
                            .layerCount = 1,
                            .colorAttachmentCount = 1,
                            .pColorAttachments = &attachment});
    const float scale = std::min(float(size.width) / display.width,
                                 float(size.height) / display.height);
    const float width = display.width * scale, height = display.height * scale;
    const vk::Viewport viewport{(size.width - width) * .5f,
                                (size.height - height) * .5f,
                                width,
                                height,
                                0,
                                1};
    const vk::Rect2D scissor{{0, 0}, size};
    command.setViewport(0, viewport);
    command.setScissor(0, scissor);
    command.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    const vk::DescriptorImageInfo sampled{
        *sampler, source_view, vk::ImageLayout::eShaderReadOnlyOptimal};
    const vk::WriteDescriptorSet binding{
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &sampled};
    command.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, *layout, 0,
                                 binding);
    command.draw(3, 1, 0, 0);
    command.endRendering();
    auto after = before;
    after.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    after.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    after.dstStageMask = vk::PipelineStageFlagBits2::eNone;
    after.dstAccessMask = {};
    after.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    after.newLayout = vk::ImageLayout::ePresentSrcKHR;
    command.pipelineBarrier2(
        {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &after});
    Vulkan::SubmitInfo submit{};
    submit.AddSignal(*ready, 0);
    submit.AddSignal(*completed);
    result.tick = scheduler.CurrentTick();
    const auto recorded_at = kt::diagnostics::MonotonicMicroseconds();
    scheduler.Flush(submit);
    gpu_pending = true;
    const auto submitted_at = kt::diagnostics::MonotonicMicroseconds();
    const auto handle = *chain;
    const vk::Fence fence = *presented;
    const vk::SwapchainPresentFenceInfoEXT completion{.swapchainCount = 1,
                                                      .pFences = &fence};
    const auto semaphore = *ready;
    const auto status =
        instance.GetPresentQueue().presentKHR({.pNext = &completion,
                                               .waitSemaphoreCount = 1,
                                               .pWaitSemaphores = &semaphore,
                                               .swapchainCount = 1,
                                               .pSwapchains = &handle,
                                               .pImageIndices = &index});
    if (status == vk::Result::eSuccess ||
        status == vk::Result::eSuboptimalKHR ||
        status == vk::Result::eErrorOutOfDateKHR ||
        status == vk::Result::eErrorSurfaceLostKHR) {
      present_pending = true;
    } else {
      Check(status, "Queue tracked presentation");
    }
    const auto queued_at = kt::diagnostics::MonotonicMicroseconds();
    Check(device.waitForFences(*completed, true, 5'000'000'000ull),
          "Wait presentation submit");
    gpu_pending = false;
    const auto gpu_completed_at = kt::diagnostics::MonotonicMicroseconds();
    Check(device.waitForFences(*presented, true, 5'000'000'000ull),
          "Wait presentation engine");
    present_pending = false;
    const auto presentation_completed_at = kt::diagnostics::MonotonicMicroseconds();
    initialized[index] = true;
    if (status == vk::Result::eErrorOutOfDateKHR) {
      RetireChain();
      result.status = RuntimePresentStatus::OutOfDate;
      return result;
    }
    Check(status == vk::Result::eSuboptimalKHR ? vk::Result::eSuccess : status,
          "Complete tracked presentation");
    result.status = RuntimePresentStatus::Completed;
    result.completed_presents = ++presents;
    const auto completed_at = kt::diagnostics::MonotonicMicroseconds();
    result.timing = {true, completed_at-began, prepared_at-began, retired_at-prepared_at,
        acquired_at-retired_at, recorded_at-acquired_at, submitted_at-recorded_at,
        queued_at-submitted_at, gpu_completed_at-queued_at,
        presentation_completed_at-gpu_completed_at, completed_at-presentation_completed_at};
    return result;
  }
};
Presenter::Presenter(const Vulkan::Instance &instance,
                     const Frontend::WindowSDL &window,
                     Vulkan::Scheduler &scheduler, Vulkan::Runtime &runtime,
                     VideoCore::TextureCache &cache, bool vsync)
    : impl{std::make_unique<Impl>(instance, window, scheduler, runtime,
                                  cache, vsync)} {}
Presenter::~Presenter() = default;
RuntimePresentResult Presenter::Present(const RuntimeDisplay &display) {
  return impl->Present(display);
}
void Presenter::Drain() { impl->Drain(); }
} // namespace Ketsui::GpuCore
