// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/renderer_vulkan/vk_instance.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>

namespace Ketsui::GpuCore {
// The input caller and presentation worker share this small object. No pad,
// scheduler or snapshot lock may be held while sleeping. Swapchain replacement
// takes the same mutex, so a driver call never uses a retired chain.
class Reflex {
  vk::Device device;
  std::shared_mutex mutex;
  std::mutex pacing_mutex;
  vk::SwapchainKHR chain;
  vk::UniqueSemaphore semaphore;
  std::atomic<std::uint64_t> epoch{1};
  std::uint64_t sampled{}, signal{}, pending{};
  std::uint64_t rendered{}; // GPU owner only.
  std::atomic<bool> active{};
  void MarkUnlocked(std::uint64_t frame, vk::LatencyMarkerNV marker) {
    if (active.load(std::memory_order_relaxed)) {
      const vk::SetLatencyMarkerInfoNV info{.presentID = frame, .marker = marker};
      device.setLatencyMarkerNV(chain, &info);
    }
  }
public:
  explicit Reflex(vk::Device device_) : device{device_} {}
  void NextFrame() { epoch.fetch_add(1, std::memory_order_release); }
  std::uint64_t CurrentFrame() const { return epoch.load(std::memory_order_acquire); }
  void BeginRendering() {
    const auto frame = CurrentFrame();
    if (rendered == frame) return;
    rendered = frame;
    std::shared_lock lock(mutex);
    MarkUnlocked(frame, vk::LatencyMarkerNV::eSimulationEnd);
    MarkUnlocked(frame, vk::LatencyMarkerNV::eRendersubmitStart);
  }
  void Mark(std::uint64_t frame, vk::LatencyMarkerNV marker) {
    std::shared_lock lock(mutex);
    MarkUnlocked(frame, marker);
  }
  void Connect(vk::SwapchainKHR swapchain, bool supported) {
    std::lock_guard lock(mutex);
    chain = swapchain;
    if (supported) {
      const vk::SemaphoreTypeCreateInfo type{.semaphoreType = vk::SemaphoreType::eTimeline};
      auto [result, created] = device.createSemaphoreUnique({.pNext = &type});
      if (result == vk::Result::eSuccess) {
        semaphore = std::move(created);
        const vk::LatencySleepModeInfoNV mode{.lowLatencyMode = true};
        result = device.setLatencySleepModeNV(chain, &mode);
        active = result == vk::Result::eSuccess;
      }
      std::fprintf(stderr, "REFLEX backend=ps4-vulkan active=%s result=%d\n", active ? "true" : "false", int(result));
    } else {
      std::fprintf(stderr, "REFLEX backend=ps4-vulkan active=false reason=configuration_or_capability\n");
    }
  }
  void Disconnect() {
    std::lock_guard lock(mutex);
    active = false;
    if (pending) {
      const auto handle = *semaphore;
      const vk::SemaphoreWaitInfo wait{.semaphoreCount = 1, .pSemaphores = &handle, .pValues = &pending};
      if (device.waitSemaphores(wait, 5'000'000'000ull) != vk::Result::eSuccess)
        throw std::runtime_error("Reflex sleep did not retire");
    }
    // The old swapchain owns its mode. Do not pass a null mode pointer: some
    // Windows drivers dereference it even though older specifications allow it.
    semaphore.reset(); chain = nullptr; pending = signal = sampled = 0;
  }
  void Pace() {
    std::lock_guard pace_lock(pacing_mutex);
    // A pending sleep must not block the previous frame's present markers.
    // Only swapchain replacement/destruction requires the exclusive lock.
    std::shared_lock lock(mutex);
    const auto frame = epoch.load(std::memory_order_acquire);
    if (!active || sampled == frame) return;
    sampled = frame;
    const auto handle = *semaphore;
    const vk::LatencySleepInfoNV sleep{.signalSemaphore = handle, .value = ++signal};
    auto result = device.latencySleepNV(chain, sleep);
    if (result == vk::Result::eSuccess) {
      pending = signal;
      const vk::SemaphoreWaitInfo wait{.semaphoreCount = 1, .pSemaphores = &handle, .pValues = &pending};
      result = device.waitSemaphores(wait, 50'000'000ull);
      if (result == vk::Result::eSuccess) pending = 0;
    }
    if (result != vk::Result::eSuccess) {
      const vk::LatencySleepModeInfoNV disabled{};
      (void)device.setLatencySleepModeNV(chain, &disabled);
      active = false;
      std::fprintf(stderr, "REFLEX backend=ps4-vulkan active=false reason=sleep result=%d\n", int(result));
    } else if (signal == 120) {
      std::fprintf(stderr, "REFLEX backend=ps4-vulkan sampled_frames=120 timeline_completed=true\n");
    }
    MarkUnlocked(frame, vk::LatencyMarkerNV::eSimulationStart);
    MarkUnlocked(frame, vk::LatencyMarkerNV::eInputSample);
  }
};
}
