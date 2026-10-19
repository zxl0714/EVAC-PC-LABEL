#include "image_runtime_tests.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vk_mem_alloc.h>

namespace Ketsui::GpuCore {
ImageRuntimeResults TestImageRuntime(const Vulkan::Instance &instance) {
  ImageRuntimeResults results;
  const auto check = [&](bool ok, const char *text) {
    if (!ok) {
      throw std::runtime_error(text);
    }
    ++results.checks;
  };
  VmaTotalStatistics before{};
  vmaCalculateStatistics(instance.GetAllocator(), &before);
  {
    Vulkan::Scheduler scheduler{instance};
    Vulkan::Runtime runtime{instance, scheduler};
    Common::SlotVector<VideoCore::ImageView> views;
    VideoCore::ImageInfo info{};
    info.pixel_format = vk::Format::eR8G8B8A8Unorm;
    info.type = AmdGpu::ImageType::Color2D;
    info.size = {128, 64, 1};
    info.pitch = 128;
    info.num_bits = 32;
    info.num_samples = 1;
    info.tile_mode = AmdGpu::TileMode::DisplayLinearAligned;
    info.array_mode = AmdGpu::GetArrayMode(info.tile_mode);
    info.guest_address = 0x1100000000ull;
    info.UpdateSize();
    VideoCore::Image a{instance, runtime, views, info};
    info.guest_address += 0x10000;
    VideoCore::Image b{instance, runtime, views, info};
    const auto image_a = a.GetImage(), image_b = b.GetImage();
    check(image_a && image_b && image_a != image_b,
          "Could not create resident image pair");
    constexpr u64 bytes = 128 * 64 * 4;
    auto first = runtime.GetStagingPool().Request(
        bytes, VideoCore::MemoryType::HostCached, 256, true);
    auto second = runtime.GetStagingPool().Request(
        bytes, VideoCore::MemoryType::HostCached, 256, true);
    vk::BufferImageCopy copy{
        .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                             .layerCount = 1},
        .imageExtent = {128, 64, 1},
    };
    const auto download = [&](VideoCore::Image &image,
                              const Vulkan::StagingBufferRef &dst) {
      copy.bufferOffset = dst.offset;
      runtime.DownloadImage(&image, dst.buffer, {&copy, 1});
    };
    const auto verify = [&](const Vulkan::StagingBufferRef &ref, u32 expected) {
      ref.Invalidate();
      const auto *words = reinterpret_cast<const u32 *>(ref.mapped);
      check(std::all_of(words, words + bytes / sizeof(u32),
                        [expected](u32 v) { return v == expected; }),
            "Resident image transfer content mismatch");
      results.verified_bytes += bytes;
    };
    runtime.ClearImage(
        &a, {},
        vk::ClearValue{.color = {.float32 = std::array<float, 4>{1, 0, 0, 1}}});
    runtime.CopyImage(&a, &b);
    runtime.ClearImage(
        &a, {},
        vk::ClearValue{.color = {.float32 = std::array<float, 4>{0, 1, 0, 1}}});
    download(a, first);
    download(b, second);
    runtime.FlushBarriers();
    scheduler.Finish();
    verify(first, 0xff00ff00);
    verify(second, 0xff0000ff);
    auto upload = runtime.GetStagingPool().Request(
        bytes, VideoCore::MemoryType::HostUncached, 256);
    std::fill_n(reinterpret_cast<u32 *>(upload.mapped), bytes / sizeof(u32),
                0xffff0000);
    upload.Flush();
    copy.bufferOffset = upload.offset;
    runtime.UploadImage(&a, upload.buffer, {&copy, 1});
    runtime.CopyImage(&a, &b);
    runtime.ClearImage(
        &a, {},
        vk::ClearValue{.color = {.float32 = std::array<float, 4>{0, 0, 0, 1}}});
    download(a, first);
    download(b, second);
    runtime.FlushBarriers();
    scheduler.Finish();
    verify(first, 0xff000000);
    verify(second, 0xffff0000);
    check(a.GetImage() == image_a && b.GetImage() == image_b,
          "Image transfer recreated resident resources");
    runtime.GetStagingPool().FreeDeferred(first);
    runtime.GetStagingPool().FreeDeferred(second);
    runtime.FlushBarriers();
    scheduler.Finish();
  }
  VmaTotalStatistics after{};
  vmaCalculateStatistics(instance.GetAllocator(), &after);
  check(after.total.statistics.allocationCount ==
            before.total.statistics.allocationCount,
        "Image runtime leaked Vulkan allocations");
  return results;
}
} // namespace Ketsui::GpuCore
