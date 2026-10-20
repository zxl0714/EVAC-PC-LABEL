#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "buffer_pool_tests.h"
#include "cache_tests.h"
#include "host_services.h"
#include "image_runtime_tests.h"
#include "native_memory_tests.h"
#include "sdl_window.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include <vk_mem_alloc.h>

namespace {
void Require(bool result, const char *message) {
  if (!result) {
    throw std::runtime_error(message);
  }
}

class Watchdog {
  std::mutex mutex;
  std::condition_variable_any wake;
  std::jthread worker{[this](std::stop_token stop) {
    std::unique_lock lock{mutex};
    wake.wait_for(lock, stop, std::chrono::seconds(20), [] { return false; });
    if (!stop.stop_requested()) {
      std::fputs("gpu_core_smoke: timeout\n", stderr);
      std::abort();
    }
  }};

public:
  ~Watchdog() { worker.request_stop(); }
};

class ReadbackBuffer {
  VmaAllocator allocator;

public:
  VkBuffer buffer{};
  VmaAllocation allocation{};
  explicit ReadbackBuffer(VmaAllocator allocator) : allocator{allocator} {
    const VkBufferCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = 8192,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    const VmaAllocationCreateInfo memory{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        .preferredFlags = 0,
        .memoryTypeBits = 0,
        .pool = nullptr,
        .pUserData = nullptr,
        .priority = 0,
    };
    Require(vmaCreateBuffer(allocator, &info, &memory, &buffer, &allocation,
                            nullptr) == VK_SUCCESS,
            "Could not allocate readback buffer");
  }
  ~ReadbackBuffer() { vmaDestroyBuffer(allocator, buffer, allocation); }
};
} // namespace

int main(int argc, char **argv) {
  try {
    Watchdog watchdog;
    const bool validation =
        argc == 2 && std::string_view{argv[1]} == "--validation";
    Require(argc == 1 || validation,
            "Usage: KetsuiGpuCoreSmoke [--validation]");
    Ketsui::GpuCore::ConfigureHost(
        {.log_directory = std::filesystem::current_path()});
    std::atomic<unsigned> priority_callbacks{0};
    unsigned ordinary_callbacks = 0;
    unsigned submissions = 0;
    std::uint64_t last_tick = 0;
    double blocked_flush_max_ms = 0;
    std::string gpu_name;
    Ketsui::GpuCore::BufferPoolResults buffer_results;
    Ketsui::GpuCore::ImageRuntimeResults image_results;
    Ketsui::GpuCore::CacheResults cache_results;
    Ketsui::GpuCore::CacheResults native_results;
    {
      Frontend::WindowSDL headless;
      Vulkan::Instance instance{headless, -1, validation};
      gpu_name = instance.GetModelName();
      ReadbackBuffer readback{instance.GetAllocator()};
      const vk::SemaphoreTypeCreateInfo gate_type{
          .semaphoreType = vk::SemaphoreType::eTimeline,
          .initialValue = 0,
      };
      auto gate = Vulkan::Check(
          instance.GetDevice().createSemaphoreUnique({.pNext = &gate_type}));
      {
        Vulkan::Scheduler scheduler{instance};
        scheduler.SetSubmitCallback(
            [&](Vulkan::SubmitInfo &) { ++submissions; });
        constexpr unsigned passes = 2;
        constexpr unsigned batches = 8;
        constexpr unsigned commands = 256;
        for (unsigned pass = 0; pass < passes; ++pass) {
          for (unsigned batch = 0; batch < batches; ++batch) {
            auto command = scheduler.CommandBuffer();
            const vk::Buffer buffer{readback.buffer};
            for (unsigned i = 0; i < commands; ++i) {
              command.fillBuffer(
                  buffer, (batch * commands + i) * sizeof(u32), sizeof(u32),
                  0xcafe0000u + (pass * batches + batch) * commands + i);
            }
            const vk::BufferMemoryBarrier barrier{
                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = vk::AccessFlagBits::eHostRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = buffer,
                .offset = batch * commands * sizeof(u32),
                .size = commands * sizeof(u32),
            };
            command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                    vk::PipelineStageFlagBits::eHost, {}, {},
                                    barrier, {});
            scheduler.DeferOperation([&] { ++ordinary_callbacks; });
            scheduler.DeferPriorityOperation([&] { ++priority_callbacks; });
            last_tick = scheduler.CurrentTick();
            Vulkan::SubmitInfo info{};
            if (batch == 0) {
              info.AddWait(*gate, pass + 1);
            }
            const auto before = std::chrono::steady_clock::now();
            scheduler.Flush(info);
            const double flush_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - before)
                    .count();
            blocked_flush_max_ms = std::max(blocked_flush_max_ms, flush_ms);
            Require(!scheduler.IsFree(last_tick),
                    "Gate did not hold GPU completion");
            Require(ordinary_callbacks == pass * batches &&
                        priority_callbacks == pass * batches,
                    "Completion callback ran before GPU gate opened");
          }
          Vulkan::Check(instance.GetDevice().signalSemaphore(
              {.semaphore = *gate, .value = pass + 1}));
          scheduler.Wait(last_tick);
          scheduler.PopPendingOperations();
          Require(scheduler.IsFree(last_tick),
                  "Timeline did not reach submitted tick");
          Require(vmaInvalidateAllocation(instance.GetAllocator(),
                                          readback.allocation, 0,
                                          VK_WHOLE_SIZE) == VK_SUCCESS,
                  "Could not invalidate readback allocation");
          void *mapped = nullptr;
          Require(vmaMapMemory(instance.GetAllocator(), readback.allocation,
                               &mapped) == VK_SUCCESS,
                  "Could not map completed readback");
          bool equal = true;
          for (unsigned i = 0; i < batches * commands; ++i) {
            equal &= static_cast<const u32 *>(mapped)[i] ==
                     0xcafe0000u + pass * batches * commands + i;
          }
          vmaUnmapMemory(instance.GetAllocator(), readback.allocation);
          Require(equal, "GPU buffer content mismatch");
          const auto submitted_count = submissions;
          const auto next_tick = scheduler.CurrentTick();
          auto done = std::make_shared<std::promise<void>>();
          auto retired = done->get_future();
          scheduler.DeferSubmittedPriorityOperation([done] { done->set_value(); });
          Require(retired.wait_for(std::chrono::seconds(2)) == std::future_status::ready &&
                      submissions == submitted_count && scheduler.CurrentTick() == next_tick,
                  "Submitted callback barrier required an extra GPU submission");
          retired.get();
          const auto queries_before_idle = scheduler.HostTimelineQueryCount();
          for (unsigned idle = 0; idle < 1000; ++idle) { scheduler.PopPendingOperations(); }
          Require(scheduler.HostTimelineQueryCount() == queries_before_idle,
                  "Idle callback retirement made redundant driver timeline queries");
          Require(priority_callbacks == (pass + 1) * batches &&
                      ordinary_callbacks == (pass + 1) * batches,
                  "Completion callbacks did not retire");
        }
        Require(submissions == passes * batches &&
                    Ketsui::GpuCore::OverlaySubmissions() == passes * batches,
                "Unexpected queue submission count");
        scheduler.Finish();
      }
      Vulkan::Check(instance.GetDevice().waitIdle());
      buffer_results = Ketsui::GpuCore::TestBufferPools(instance);
      image_results = Ketsui::GpuCore::TestImageRuntime(instance);
      cache_results = Ketsui::GpuCore::TestCaches(instance);
      native_results = Ketsui::GpuCore::TestNativeMemoryCaches(instance);
    }
    Require(Ketsui::GpuCore::ValidationErrors() == 0,
            "Vulkan reported an error");
    std::printf(
        "{\"event\":\"gpu_core_scheduler_smoke\",\"ok\":true,\"validation\":%s,"
        "\"gpu\":\"%s\",\"fillCommands\":4096,\"batches\":16,"
        "\"inFlightBatches\":8,"
        "\"lastVerifiedTick\":%llu,\"blockedFlushMaxMs\":%.3f,"
        "\"gateHeldBeforeHostSignal\":true,\"priorityCallbacks\":%u,"
        "\"ordinaryCallbacks\":%u,\"bufferPoolChecks\":%u,"
        "\"bufferPoolVerifiedBytes\":%llu,"
        "\"imageRuntimeChecks\":%u,\"imageRuntimeVerifiedBytes\":%llu,"
        "\"cacheChecks\":%u,\"cacheReadFaults\":%u,\"cacheWriteFaults\":%u,"
        "\"cacheVerifiedBytes\":%llu,"
        "\"nativeMemoryChecks\":%u,\"nativeMemoryReadFaults\":%u,"
        "\"nativeMemoryWriteFaults\":%u,\"nativeMemoryVerifiedBytes\":%llu,"
        "\"recompilerChecks\":%u,\"recompilerModules\":%u,"
        "\"recompilerSpirvWords\":%llu,"
        "\"validationErrors\":%u}\n",
        validation ? "true" : "false", gpu_name.c_str(),
        static_cast<unsigned long long>(last_tick), blocked_flush_max_ms,
        priority_callbacks.load(), ordinary_callbacks, buffer_results.checks,
        static_cast<unsigned long long>(buffer_results.verified_bytes),
        image_results.checks,
        static_cast<unsigned long long>(image_results.verified_bytes),
        cache_results.checks, cache_results.read_faults,
        cache_results.write_faults,
        static_cast<unsigned long long>(cache_results.verified_bytes),
        native_results.checks, native_results.read_faults,
        native_results.write_faults,
        static_cast<unsigned long long>(native_results.verified_bytes),
        native_results.recompiler_checks, native_results.recompiler_modules,
        static_cast<unsigned long long>(native_results.recompiler_spirv_words),
        Ketsui::GpuCore::ValidationErrors());
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "gpu_core_smoke: %s\n", error.what());
    return 1;
  }
}
