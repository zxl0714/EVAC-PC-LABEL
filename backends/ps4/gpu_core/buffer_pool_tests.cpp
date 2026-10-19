#include "buffer_pool_tests.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <vk_mem_alloc.h>

namespace Ketsui::GpuCore {
namespace {
using VideoCore::MemoryType;
using Vulkan::StagingBufferRef;

u64 Allocations(const Vulkan::Instance &instance) {
  VmaTotalStatistics stats{};
  vmaCalculateStatistics(instance.GetAllocator(), &stats);
  return stats.total.statistics.allocationCount;
}

bool NonOverlapping(const std::vector<StagingBufferRef> &refs) {
  for (std::size_t i = 0; i < refs.size(); ++i) {
    for (std::size_t j = i + 1; j < refs.size(); ++j) {
      if (refs[i].buffer == refs[j].buffer &&
          refs[i].offset < refs[j].offset + refs[j].size &&
          refs[j].offset < refs[i].offset + refs[i].size) {
        return false;
      }
    }
  }
  return true;
}

void Barrier(vk::CommandBuffer cmd, const StagingBufferRef &ref, bool to_host) {
  const vk::BufferMemoryBarrier barrier{
      .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
      .dstAccessMask = to_host ? vk::AccessFlagBits::eHostRead
                               : vk::AccessFlagBits::eTransferRead,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = ref.buffer->Handle(),
      .offset = ref.offset,
      .size = ref.size,
  };
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                      to_host ? vk::PipelineStageFlagBits::eHost
                              : vk::PipelineStageFlagBits::eTransfer,
                      {}, {}, barrier, {});
}

class GateDrain {
  const Vulkan::Instance &instance;
  Vulkan::Scheduler &scheduler;
  vk::Semaphore semaphore;
  u64 signalled{};

public:
  GateDrain(const Vulkan::Instance &instance, Vulkan::Scheduler &scheduler,
            vk::Semaphore semaphore)
      : instance{instance}, scheduler{scheduler}, semaphore{semaphore} {}
  void Signal(u64 value) {
    Vulkan::Check(instance.GetDevice().signalSemaphore(
        {.semaphore = semaphore, .value = value}));
    signalled = value;
  }
  ~GateDrain() {
    if (signalled < 2) {
      Signal(2);
    }
    scheduler.Finish();
  }
};
} // namespace

BufferPoolResults TestBufferPools(const Vulkan::Instance &instance) {
  BufferPoolResults results;
  const auto check = [&](bool ok, const char *text) {
    if (!ok) {
      throw std::runtime_error(text);
    }
    ++results.checks;
  };
  const u64 initial_allocations = Allocations(instance);
  {
    const vk::SemaphoreTypeCreateInfo gate_type{
        .semaphoreType = vk::SemaphoreType::eTimeline, .initialValue = 0};
    auto gate = Vulkan::Check(
        instance.GetDevice().createSemaphoreUnique({.pNext = &gate_type}));
    Vulkan::Scheduler scheduler{instance};
    Vulkan::StagingBufferPool pool{instance, scheduler};
    GateDrain drain{instance, scheduler, *gate};
    constexpr u64 chunk = 4_MB;
    std::vector<StagingBufferRef> uploads, device_buffers, downloads;
    u64 last_tick{};
    for (u32 batch = 0; batch < 5; ++batch) {
      uploads.push_back(pool.Request(chunk, MemoryType::HostUncached, 256));
      device_buffers.push_back(
          pool.Request(chunk, MemoryType::DeviceLocal, 256));
      downloads.push_back(pool.Request(chunk, MemoryType::HostCached, 256));
      const auto &upload = uploads.back();
      const auto &device = device_buffers.back();
      const auto &download = downloads.back();
      check(upload.mapped && download.mapped && upload.offset % 256 == 0 &&
                device.offset % 256 == 0 && download.offset % 256 == 0,
            "Staging allocation mapping/alignment failed");
      std::fill_n(reinterpret_cast<u32 *>(upload.mapped), chunk / sizeof(u32),
                  0x51000000u + batch);
      upload.Flush();
      auto cmd = scheduler.CommandBuffer();
      const vk::BufferCopy to_device{.srcOffset = upload.offset,
                                     .dstOffset = device.offset,
                                     .size = chunk};
      cmd.copyBuffer(upload.buffer->Handle(), device.buffer->Handle(),
                     to_device);
      Barrier(cmd, device, false);
      const vk::BufferCopy to_host{.srcOffset = device.offset,
                                   .dstOffset = download.offset,
                                   .size = chunk};
      cmd.copyBuffer(device.buffer->Handle(), download.buffer->Handle(),
                     to_host);
      Barrier(cmd, download, true);
      last_tick = scheduler.CurrentTick();
      Vulkan::SubmitInfo info{};
      if (batch == 0) {
        info.AddWait(*gate, 1);
      }
      scheduler.Flush(info);
      check(!scheduler.IsFree(last_tick),
            "Staging workload completed through closed gate");
    }
    check(NonOverlapping(uploads) && NonOverlapping(device_buffers) &&
              NonOverlapping(downloads),
          "Staging ring overwrote an in-flight allocation");
    check(uploads[0].buffer != uploads[4].buffer &&
              device_buffers[0].buffer != device_buffers[4].buffer &&
              downloads[0].buffer != downloads[4].buffer,
          "Busy full staging ring did not grow");
    drain.Signal(1);
    scheduler.Wait(last_tick);
    for (u32 batch = 0; batch < 5; ++batch) {
      downloads[batch].Invalidate();
      const auto *words =
          reinterpret_cast<const u32 *>(downloads[batch].mapped);
      check(std::all_of(
                words, words + chunk / sizeof(u32),
                [batch](u32 word) { return word == 0x51000000u + batch; }),
            "Staged CPU -> GPU -> CPU round trip mismatch");
      results.verified_bytes += chunk;
    }
    uploads.clear();
    device_buffers.clear();
    downloads.clear();
    scheduler.Finish();
    for (u32 i = 0; i < 301; ++i) {
      pool.TickFrame();
    }
    check(Allocations(instance) == initial_allocations,
          "Completed idle staging rings did not trim");

    constexpr u64 held_size = 4096;
    auto first = pool.Request(held_size, MemoryType::HostCached, 256, true);
    auto second = pool.Request(held_size, MemoryType::HostCached, 256, true);
    check(first.buffer != second.buffer, "Held readback allocation was reused");
    auto large_first = pool.Request(17_MB, MemoryType::DeviceLocal);
    auto large_second = pool.Request(17_MB, MemoryType::DeviceLocal);
    check(large_first.buffer != large_second.buffer,
          "Current-tick large allocation was reused");
    auto cmd = scheduler.CommandBuffer();
    cmd.fillBuffer(first.buffer->Handle(), first.offset, held_size, 0x5eedaffe);
    Barrier(cmd, first, true);
    pool.FreeDeferred(first);
    last_tick = scheduler.CurrentTick();
    Vulkan::SubmitInfo info{};
    info.AddWait(*gate, 2);
    scheduler.Flush(info);
    auto third = pool.Request(held_size, MemoryType::HostCached, 256, true);
    check(third.buffer != first.buffer && third.buffer != second.buffer &&
              !scheduler.IsFree(last_tick),
          "Released in-flight or held readback allocation was recycled");
    const u64 busy_allocations = Allocations(instance);
    for (u32 i = 0; i < 301; ++i) {
      pool.TickFrame();
    }
    check(Allocations(instance) == busy_allocations,
          "Idle trimming freed a held or in-flight allocation");
    drain.Signal(2);
    scheduler.Wait(last_tick);
    first.Invalidate();
    const auto *words = reinterpret_cast<const u32 *>(first.mapped);
    check(std::all_of(words, words + held_size / sizeof(u32),
                      [](u32 word) { return word == 0x5eedaffe; }),
          "Deferred readback content changed before completion");
    results.verified_bytes += held_size;
    auto fourth = pool.Request(held_size, MemoryType::HostCached, 256, true);
    auto large_reused = pool.Request(17_MB, MemoryType::DeviceLocal);
    check(fourth.buffer == first.buffer && fourth.buffer != second.buffer &&
              fourth.buffer != third.buffer,
          "Completed released allocation did not reuse safely");
    check(large_reused.buffer == large_first.buffer,
          "Completed large allocation was not reused");
    pool.FreeDeferred(second);
    pool.FreeDeferred(third);
    pool.FreeDeferred(fourth);
    scheduler.Finish();
    for (u32 i = 0; i < 121; ++i) {
      pool.TickFrame();
    }
    check(Allocations(instance) == initial_allocations,
          "Released large allocations did not trim");
  }
  check(Allocations(instance) == initial_allocations,
        "Staging cleanup leaked Vulkan allocations");
  return results;
}
} // namespace Ketsui::GpuCore
