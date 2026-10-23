// SPDX-License-Identifier: GPL-2.0-or-later
#include "presenter.h"
#include "cache_memory.h"
#include "core/libraries/videoout/buffer.h"
#include "diagnostics/monotonic_clock.h"
#include "diagnostics/timing_trace.h"
#include "graphics/present_mode.h"
#include "sdl_window.h"
#include "video_core/host_shaders/fs_tri_vert.h"
#include "video_core/host_shaders/host_present_frag.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"
#include "video_core/texture_cache/texture_cache.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

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
  enum class SnapshotState { Free, Writing, Ready, Presenting };
  struct Snapshot {
    vk::UniqueFence copied;
    std::unique_ptr<VideoCore::UniqueImage> snapshot;
    vk::UniqueImageView snapshot_view;
    vk::Extent2D snapshot_extent{};
    vk::Format snapshot_format{};
    bool snapshot_initialized{}, copy_pending{};
    u64 copy_tick{}, sequence{};
    SnapshotState state = SnapshotState::Free; // guarded by handoff_mutex
  };
  struct FrameResources {
    vk::UniqueFence acquired, completed, presented;
    vk::UniqueSemaphore ready;
    vk::UniqueCommandPool pool;
    vk::UniqueCommandBuffer command;
    bool gpu_pending{}, present_pending{};
  };
  std::array<Snapshot, PresentSnapshotSlots> snapshots;
  std::array<FrameResources, PresentResourceSlots> frames;
  u32 next_frame{};
  bool vsync = true;
  u64 presents{};
  const vk::Semaphore copy_timeline;
  const vk::Queue output_queue;
  std::mutex handoff_mutex;
  std::condition_variable handoff_changed;
  std::optional<u32> ready_snapshot;
  RuntimePresentationStats stats;
  std::exception_ptr presentation_error;
  u64 drain_request{}, drained{};
  bool diagnostic_paused{}, stopping{};
  struct PendingFrame {
    RuntimePresentResult result;
    u32 frame;
    std::uint64_t began, prepared_at, retired_at, recorded_at, submitted_at;
  };
  std::optional<PendingFrame> pending;
  std::thread presentation_thread;
  Impl(const Vulkan::Instance &instance_, const Frontend::WindowSDL &window_,
       Vulkan::Scheduler &scheduler_, Vulkan::Runtime &runtime_,
       VideoCore::TextureCache &cache_, bool vsync_, bool shared_present_queue)
      : instance{instance_},
        window{static_cast<HWND>(window_.GetWindowInfo().render_surface)},
        scheduler{scheduler_}, runtime{runtime_}, cache{cache_},
        surface{instance.GetInstance(), window_}, vsync{vsync_},
        copy_timeline{scheduler.GetWorkSemaphore()->Handle()},
        output_queue{shared_present_queue ? instance.GetGraphicsQueue()
                                          : instance.GetPresentQueue()} {
    Require(window && IsWindow(window) && instance.HasPresentFences(),
            "Presenter needs a live window and present fences");
    Require(Checked(instance.GetPhysicalDevice().getSurfaceSupportKHR(
                        instance.GetGraphicsQueueFamilyIndex(), surface.value),
                    "Query present queue"),
            "Graphics queue cannot present to the window");
    const auto device = instance.GetDevice();
    for (auto &frame : frames) {
      frame.acquired =
          Checked(device.createFenceUnique({}), "Create acquire fence");
      frame.completed =
          Checked(device.createFenceUnique({}), "Create submit fence");
      frame.presented =
          Checked(device.createFenceUnique({}), "Create presentation fence");
      frame.ready =
          Checked(device.createSemaphoreUnique({}), "Create present semaphore");
      frame.pool = Checked(
          device.createCommandPoolUnique(
              {.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
               .queueFamilyIndex = instance.GetPresentQueueFamilyIndex()}),
          "Create presentation command pool");
      auto commands = Checked(device.allocateCommandBuffersUnique(
                                  {.commandPool = *frame.pool,
                                   .level = vk::CommandBufferLevel::ePrimary,
                                   .commandBufferCount = 1}),
                              "Create presentation command buffer");
      frame.command = std::move(commands.front());
    }
    for (auto &snapshot : snapshots)
      snapshot.copied =
          Checked(device.createFenceUnique({}), "Create guest snapshot fence");
    stats.independent_queue = output_queue != instance.GetGraphicsQueue();
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
    presentation_thread = std::thread([this] { RunPresentation(); });
  }
  ~Impl() {
    std::exception_ptr failure;
    try {
      Drain();
    } catch (...) {
      failure = std::current_exception();
    }
    {
      std::lock_guard lock(handoff_mutex);
      stopping = true;
    }
    handoff_changed.notify_all();
    if (presentation_thread.joinable())
      presentation_thread.join();
    if (failure) {
      try {
        std::rethrow_exception(failure);
      } catch (const std::exception &error) {
        std::fprintf(stderr, "Presentation teardown failed: %s\n",
                     error.what());
      }
      // Do not destroy possibly in-flight GPU resources after an unretired
      // error.
      std::terminate();
    }
    views.clear();
    chain.reset();
  }
  void Retire(FrameResources &frame) {
    const auto device = instance.GetDevice();
    if (frame.gpu_pending) {
      Check(device.waitForFences(*frame.completed, true, 5'000'000'000ull),
            "Drain presentation submit");
      frame.gpu_pending = false;
    }
    if (frame.present_pending) {
      Check(device.waitForFences(*frame.presented, true, 5'000'000'000ull),
            "Retire presentation resources");
      frame.present_pending = false;
      std::lock_guard lock(handoff_mutex);
      ++stats.retired_resources;
      --stats.retained_resources;
    }
  }
  void Drain() {
    {
      std::unique_lock lock(handoff_mutex);
      diagnostic_paused = false;
      const auto ticket = ++drain_request;
      handoff_changed.notify_all();
      Require(handoff_changed.wait_for(
                  lock, std::chrono::seconds(10),
                  [&] { return presentation_error || drained >= ticket; }),
              "Presentation worker did not drain");
      if (presentation_error)
        std::rethrow_exception(presentation_error);
    }
    for (auto &snapshot : snapshots) {
      if (!snapshot.copy_pending)
        continue;
      Check(instance.GetDevice().waitForFences(*snapshot.copied, true,
                                               5'000'000'000ull),
            "Drain guest snapshot");
      snapshot.copy_pending = false;
    }
  }
  RuntimePresentationStats Stats() {
    std::lock_guard lock(handoff_mutex);
    if (presentation_error)
      std::rethrow_exception(presentation_error);
    return stats;
  }
  void PauseForDiagnostics(bool paused) {
    std::lock_guard lock(handoff_mutex);
    if (presentation_error)
      std::rethrow_exception(presentation_error);
    diagnostic_paused = paused;
    handoff_changed.notify_all();
  }
  void RecordSnapshot(Snapshot &frame, const RuntimeDisplay &display,
                      VideoCore::TextureCache::ImageDesc &desc) {
    const vk::Extent2D size{display.width, display.height};
    const auto source_format = display.pixel_format == 0x80000000
                                   ? vk::Format::eB8G8R8A8Srgb
                                   : vk::Format::eR8G8B8A8Srgb;
    if (!frame.snapshot || frame.snapshot_extent != size ||
        frame.snapshot_format != source_format) {
      // This slot's prior GPU reader was retired before reaching here.
      frame.snapshot_view.reset();
      frame.snapshot = std::make_unique<VideoCore::UniqueImage>(
          instance.GetDevice(), instance.GetAllocator());
      frame.snapshot->Create({.imageType = vk::ImageType::e2D,
                              .format = source_format,
                              .extent = {size.width, size.height, 1},
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = vk::SampleCountFlagBits::e1,
                              .tiling = vk::ImageTiling::eOptimal,
                              .usage = vk::ImageUsageFlagBits::eTransferDst |
                                       vk::ImageUsageFlagBits::eTransferSrc |
                                       vk::ImageUsageFlagBits::eSampled,
                              .sharingMode = vk::SharingMode::eExclusive});
      frame.snapshot_view =
          Checked(instance.GetDevice().createImageViewUnique(
                      {.image = frame.snapshot->image,
                       .viewType = vk::ImageViewType::e2D,
                       .format = source_format,
                       .components = {.a = vk::ComponentSwizzle::eOne},
                       .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0,
                                            1, 0, 1}}),
                  "Create snapshot view");
      frame.snapshot_extent = size;
      frame.snapshot_format = source_format;
      frame.snapshot_initialized = false;
    }
    const auto image_id = cache.FindImage(desc);
    cache.UpdateImage(image_id);
    auto &image = cache.GetImage(image_id);
    Require(image.info.num_samples == 1 && image.info.num_bits == 32 &&
                image.info.size.width >= size.width &&
                image.info.size.height >= size.height,
            "Guest display is not a single-sample 32-bit snapshot source");
    scheduler.EndRendering();
    runtime.Transit(&image, vk::ImageLayout::eTransferSrcOptimal,
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferRead);
    runtime.FlushBarriers();
    const auto command = scheduler.CommandBuffer();
    const vk::ImageMemoryBarrier2 before{
        .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = frame.snapshot_initialized
                         ? vk::ImageLayout::eShaderReadOnlyOptimal
                         : vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = frame.snapshot->image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    command.pipelineBarrier2(
        {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &before});
    const vk::ImageCopy copy{
        .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .extent = {size.width, size.height, 1}};
    command.copyImage(image.GetImage(), vk::ImageLayout::eTransferSrcOptimal,
                      frame.snapshot->image,
                      vk::ImageLayout::eTransferDstOptimal, copy);
    auto after = before;
    after.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    after.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    after.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
    after.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    after.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    after.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    command.pipelineBarrier2(
        {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &after});
    frame.snapshot_initialized = true;
  }
  void ReadPreparedSnapshot(void *output, std::uint64_t bytes) {
    Require(pending.has_value() && output,
            "Snapshot readback requires a prepared frame");
    auto &frame = snapshots[pending->frame];
    const auto size = frame.snapshot_extent;
    Require(bytes == std::uint64_t{size.width} * size.height * 4,
            "Snapshot diagnostic readback size mismatch");
    const auto download = runtime.GetStagingPool().Request(
        bytes, VideoCore::MemoryType::HostCached);
    scheduler.EndRendering();
    const auto command = scheduler.CommandBuffer();
    const vk::ImageMemoryBarrier2 before{
        .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = frame.snapshot->image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    command.pipelineBarrier2(
        {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &before});
    const vk::BufferImageCopy copy{
        .bufferOffset = download.offset,
        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .imageExtent = {size.width, size.height, 1}};
    command.copyImageToBuffer(frame.snapshot->image,
                              vk::ImageLayout::eTransferSrcOptimal,
                              download.buffer->Handle(), copy);
    auto after = before;
    std::swap(after.srcStageMask, after.dstStageMask);
    std::swap(after.srcAccessMask, after.dstAccessMask);
    std::swap(after.oldLayout, after.newLayout);
    command.pipelineBarrier2(
        {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &after});
    scheduler.Finish();
    download.Invalidate();
    std::memcpy(output, download.mapped, bytes);
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
    for (auto &frame : frames)
      Retire(frame);
    views.clear();
    chain.reset();
    images.clear();
    initialized.clear();
  }
  vk::PresentModeKHR SelectMode() const {
    const auto modes = Checked(
        instance.GetPhysicalDevice().getSurfacePresentModesKHR(surface.value),
        "Query present modes");
    const auto selected = cave::ps4::SelectPresentMode(
        vsync, std::span<const vk::PresentModeKHR>(modes),
        vk::PresentModeKHR::eFifo, vk::PresentModeKHR::eImmediate,
        vk::PresentModeKHR::eMailbox);
    std::fprintf(stderr, "gpu_runtime_swapchain: vsync_requested=%s mode=%s\n",
                 vsync ? "true" : "false", vk::to_string(selected).c_str());
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
  RuntimePresentResult Prepare(const RuntimeDisplay &display) {
    Require(!pending, "A prepared frame must complete before another prepare");
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
    (void)Stats(); // surface failures propagate to the owning game worker
    RECT client{};
    Require(IsWindow(window) && GetClientRect(window, &client),
            "Presentation window no longer exists");
    if (IsIconic(window) || client.right <= 0 || client.bottom <= 0) {
      return result;
    }
    result.width = static_cast<u32>(client.right);
    result.height = static_cast<u32>(client.bottom);
    const auto device = instance.GetDevice();
    const auto prepared_at = kt::diagnostics::MonotonicMicroseconds();
    u32 frame_index{};
    {
      std::lock_guard lock(handoff_mutex);
      if (presentation_error)
        std::rethrow_exception(presentation_error);
      const auto free = std::ranges::find_if(snapshots, [](const auto &item) {
        return item.state == SnapshotState::Free;
      });
      // At most one image is presenting and one is ready. The third is always
      // writable; no WSI wait may be introduced here to acquire a free slot.
      Require(free != snapshots.end(),
              "Presentation snapshot ownership exhausted");
      frame_index = static_cast<u32>(free - snapshots.begin());
      free->state = SnapshotState::Writing;
    }
    auto &frame = snapshots[frame_index];
    Require(!frame.copy_pending,
            "A free snapshot still has a pending guest copy");
    Check(device.resetFences(*frame.copied), "Reset snapshot fence");
    const auto retired_at = kt::diagnostics::MonotonicMicroseconds();
    // Snapshot guest pixels before touching WSI. The second submission samples
    // only this owned image, so its eventual completion is not a guest-buffer
    // lifetime requirement. CPU readback is never part of normal presentation.
    RecordSnapshot(frame, display, desc);
    const auto recorded_at = kt::diagnostics::MonotonicMicroseconds();
    result.tick = scheduler.CurrentTick();
    frame.copy_tick = result.tick;
    Vulkan::SubmitInfo copy_submit{};
    copy_submit.AddSignal(*frame.copied);
    scheduler.Flush(copy_submit);
    frame.copy_pending = true;
    const auto submitted_at = kt::diagnostics::MonotonicMicroseconds();
    result.status = RuntimePresentStatus::Prepared;
    pending = PendingFrame{result,     frame_index, began,       prepared_at,
                           retired_at, recorded_at, submitted_at};
    return result;
  }
  bool PresentSnapshot(u32 snapshot_index) {
    auto &snapshot = snapshots[snapshot_index];
    for (unsigned retry = 0; retry < 9; ++retry) {
      const auto began = kt::diagnostics::MonotonicMicroseconds();
      RECT client{};
      if (!IsWindow(window) || !GetClientRect(window, &client) ||
          IsIconic(window) || client.right <= 0 || client.bottom <= 0)
        return false;
      const auto caps = Checked(
          instance.GetPhysicalDevice().getSurfaceCapabilitiesKHR(surface.value),
          "Query window capabilities");
      auto size = caps.currentExtent;
      if (size.width == UINT32_MAX) {
        size = {
            std::clamp(static_cast<u32>(client.right),
                       caps.minImageExtent.width, caps.maxImageExtent.width),
            std::clamp(static_cast<u32>(client.bottom),
                       caps.minImageExtent.height, caps.maxImageExtent.height)};
      }
      if (!size.width || !size.height)
        return false;
      if (!chain || extent != size)
        CreateChain(size, caps);
      const auto device = instance.GetDevice();
      const auto frame_index = next_frame;
      auto &frame = frames[frame_index];
      Retire(frame);
      Check(device.resetFences(
                {*frame.acquired, *frame.completed, *frame.presented}),
            "Reset window fences");
      const auto retired_at = kt::diagnostics::MonotonicMicroseconds();
      u32 index{};
      const auto acquire = device.acquireNextImageKHR(
          *chain, 1'000'000'000ull, {}, *frame.acquired, &index);
      if (acquire == vk::Result::eErrorOutOfDateKHR) {
        RetireChain();
        continue;
      }
      Check(acquire == vk::Result::eSuboptimalKHR ? vk::Result::eSuccess
                                                  : acquire,
            "Acquire swapchain image");
      Check(device.waitForFences(*frame.acquired, true, 5'000'000'000ull),
            "Wait swapchain acquire");
      const auto acquired_at = kt::diagnostics::MonotonicMicroseconds();
      Require(index < images.size(), "Acquired swapchain index is invalid");
      const auto command = *frame.command;
      Check(command.reset(), "Reset window command buffer");
      Check(command.begin(
                {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}),
            "Begin window commands");
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
          .clearValue = {
              .color = {.float32 = std::array<float, 4>{0, 0, 0, 1}}}};
      command.beginRendering({.renderArea = {{0, 0}, extent},
                              .layerCount = 1,
                              .colorAttachmentCount = 1,
                              .pColorAttachments = &attachment});
      const float scale =
          std::min(float(size.width) / snapshot.snapshot_extent.width,
                   float(size.height) / snapshot.snapshot_extent.height);
      const float width = snapshot.snapshot_extent.width * scale,
                  height = snapshot.snapshot_extent.height * scale;
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
          *sampler, *snapshot.snapshot_view,
          vk::ImageLayout::eShaderReadOnlyOptimal};
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
      Check(command.end(), "End window commands");
      const auto recorded_at = kt::diagnostics::MonotonicMicroseconds();
      const vk::SemaphoreSubmitInfo wait{
          .semaphore = copy_timeline,
          .value = snapshot.copy_tick,
          .stageMask = vk::PipelineStageFlagBits2::eAllCommands};
      const vk::SemaphoreSubmitInfo signal{
          .semaphore = *frame.ready,
          .stageMask = vk::PipelineStageFlagBits2::eAllCommands};
      const vk::CommandBufferSubmitInfo commands{.commandBuffer = command};
      const vk::SubmitInfo2 submit{.waitSemaphoreInfoCount = 1,
                                   .pWaitSemaphoreInfos = &wait,
                                   .commandBufferInfoCount = 1,
                                   .pCommandBufferInfos = &commands,
                                   .signalSemaphoreInfoCount = 1,
                                   .pSignalSemaphoreInfos = &signal};
      {
        std::unique_lock lock(Vulkan::Scheduler::submit_mutex, std::defer_lock);
        if (!stats.independent_queue)
          lock.lock();
        Check(output_queue.submit2(submit, *frame.completed),
              "Submit window composition");
      }
      frame.gpu_pending = true;
      const auto submitted_at = kt::diagnostics::MonotonicMicroseconds();
      const auto handle = *chain;
      const vk::Fence fence = *frame.presented;
      const vk::SwapchainPresentFenceInfoEXT completion{.swapchainCount = 1,
                                                        .pFences = &fence};
      const auto semaphore = *frame.ready;
      vk::Result status;
      {
        std::unique_lock lock(Vulkan::Scheduler::submit_mutex, std::defer_lock);
        if (!stats.independent_queue)
          lock.lock();
        status = output_queue.presentKHR({.pNext = &completion,
                                          .waitSemaphoreCount = 1,
                                          .pWaitSemaphores = &semaphore,
                                          .swapchainCount = 1,
                                          .pSwapchains = &handle,
                                          .pImageIndices = &index});
      }
      if (status == vk::Result::eSuccess ||
          status == vk::Result::eSuboptimalKHR ||
          status == vk::Result::eErrorOutOfDateKHR ||
          status == vk::Result::eErrorSurfaceLostKHR) {
        frame.present_pending = true;
        next_frame = (frame_index + 1) % PresentResourceSlots;
        std::lock_guard lock(handoff_mutex);
        ++stats.retained_resources;
      } else {
        Check(status, "Queue tracked presentation");
      }
      const auto queued_at = kt::diagnostics::MonotonicMicroseconds();
      // This worker owns at most one active snapshot. Release it only after the
      // compositor is done reading; present-engine resource retirement is
      // separate.
      Check(device.waitForFences(*frame.completed, true, 5'000'000'000ull),
            "Retire window snapshot reader");
      frame.gpu_pending = false;
      const auto gpu_completed_at = kt::diagnostics::MonotonicMicroseconds();
      initialized[index] = true;
      if (status == vk::Result::eErrorOutOfDateKHR) {
        RetireChain();
        continue;
      }
      Check(status == vk::Result::eSuboptimalKHR ? vk::Result::eSuccess
                                                 : status,
            "Complete tracked presentation");
      {
        std::lock_guard lock(handoff_mutex);
        ++stats.queued;
        stats.last_queued_sequence = snapshot.sequence;
      }
      if (kt::diagnostics::TimingTraceEnabled()) {
        TimingLog(
            "{\"event\":\"native_gpu_window_present\",\"sequence\":%llu,\"copy_"
            "tick\":%llu,\"image\":%u,\"slot\":%u,\"thread\":%lu,\"began_clock_"
            "us\":%llu,\"retired_clock_us\":%llu,\"acquired_clock_us\":%llu,"
            "\"recorded_clock_us\":%llu,\"submitted_clock_us\":%llu,\"queued_"
            "clock_us\":%llu,\"clock_us\":%llu}\n",
            snapshot.sequence, snapshot.copy_tick, index, frame_index,
            GetCurrentThreadId(), began, retired_at, acquired_at, recorded_at,
            submitted_at, queued_at, gpu_completed_at);
      }
      return true;
    }
    throw std::runtime_error(
        "Swapchain remained out of date after eight recreations");
  }
  void RunPresentation() noexcept {
    try {
      for (;;) {
        std::unique_lock lock(handoff_mutex);
        handoff_changed.wait(lock, [&] {
          return stopping || (ready_snapshot && !diagnostic_paused) ||
                 drain_request > drained;
        });
        if (stopping)
          return;
        if (ready_snapshot && !diagnostic_paused) {
          const auto index = *ready_snapshot;
          ready_snapshot.reset();
          snapshots[index].state = SnapshotState::Presenting;
          stats.ready_images = 0;
          stats.active_images = 1;
          lock.unlock();
          const bool shown = PresentSnapshot(index);
          lock.lock();
          snapshots[index].state = SnapshotState::Free;
          stats.active_images = 0;
          if (!shown)
            ++stats.skipped;
        } else if (drain_request > drained) {
          const auto ticket = drain_request;
          lock.unlock();
          for (auto &frame : frames)
            Retire(frame);
          lock.lock();
          drained = ticket;
        }
        handoff_changed.notify_all();
      }
    } catch (...) {
      std::lock_guard lock(handoff_mutex);
      presentation_error = std::current_exception();
      handoff_changed.notify_all();
    }
  }
  RuntimePresentResult Complete(bool pacing_accepted) {
    Require(pending.has_value(), "No prepared frame to complete");
    const auto [previous, frame_index, began, prepared_at, retired_at,
                recorded_at, submitted_at] = *pending;
    auto result = previous;
    pending.reset();
    auto &frame = snapshots[frame_index];
    const auto paced_at = kt::diagnostics::MonotonicMicroseconds();
    Check(instance.GetDevice().waitForFences(*frame.copied, true,
                                             5'000'000'000ull),
          "Wait guest snapshot");
    frame.copy_pending = false;
    result.guest_copy_completed = true;
    const auto copied_at = kt::diagnostics::MonotonicMicroseconds();
    {
      std::lock_guard lock(handoff_mutex);
      if (presentation_error)
        std::rethrow_exception(presentation_error);
      if (!pacing_accepted) {
        frame.state = SnapshotState::Free;
        result.status = RuntimePresentStatus::Cancelled;
        result.tick = 0;
        return result;
      }
      if (ready_snapshot) {
        snapshots[*ready_snapshot].state = SnapshotState::Free;
        ++stats.replaced;
      }
      frame.sequence = ++presents;
      frame.state = SnapshotState::Ready;
      ready_snapshot = frame_index;
      stats.accepted = presents;
      stats.ready_images = 1;
    }
    handoff_changed.notify_all();
    result.status = RuntimePresentStatus::Completed;
    result.completed_presents = presents;
    const auto completed_at = kt::diagnostics::MonotonicMicroseconds();
    const auto between_phases_us = paced_at - submitted_at;
    result.timing = {true,
                     completed_at - began - between_phases_us,
                     prepared_at - began,
                     retired_at - prepared_at,
                     0,
                     recorded_at - retired_at,
                     submitted_at - recorded_at,
                     0,
                     copied_at - paced_at,
                     0,
                     completed_at - copied_at,
                     between_phases_us};
    if (kt::diagnostics::TimingTraceEnabled()) {
      TimingLog("{\"event\":\"native_gpu_snapshot_handoff\",\"sequence\":%llu,"
                "\"copy_tick\":%llu,\"slot\":%u,\"thread\":%lu,\"began_clock_"
                "us\":%llu,\"submitted_clock_us\":%llu,\"paced_clock_us\":%llu,"
                "\"copied_clock_us\":%llu,\"clock_us\":%llu}\n",
                presents, result.tick, frame_index, GetCurrentThreadId(), began,
                submitted_at, paced_at, copied_at, completed_at);
    }
    return result;
  }
};
Presenter::Presenter(const Vulkan::Instance &instance,
                     const Frontend::WindowSDL &window,
                     Vulkan::Scheduler &scheduler, Vulkan::Runtime &runtime,
                     VideoCore::TextureCache &cache, bool vsync,
                     bool shared_present_queue)
    : impl{std::make_unique<Impl>(instance, window, scheduler, runtime, cache,
                                  vsync, shared_present_queue)} {}
Presenter::~Presenter() = default;
RuntimePresentResult Presenter::Present(const RuntimeDisplay &display) {
  const auto result = impl->Prepare(display);
  return result.status == RuntimePresentStatus::Prepared ? impl->Complete(true)
                                                         : result;
}
RuntimePresentResult Presenter::Prepare(const RuntimeDisplay &display) {
  return impl->Prepare(display);
}
RuntimePresentResult Presenter::Complete(bool accepted) {
  return impl->Complete(accepted);
}
void Presenter::ReadPreparedSnapshot(void *output, std::uint64_t bytes) {
  impl->ReadPreparedSnapshot(output, bytes);
}
RuntimePresentationStats Presenter::Stats() { return impl->Stats(); }
void Presenter::PauseForDiagnostics(bool paused) {
  impl->PauseForDiagnostics(paused);
}
void Presenter::Drain() { impl->Drain(); }
} // namespace Ketsui::GpuCore
