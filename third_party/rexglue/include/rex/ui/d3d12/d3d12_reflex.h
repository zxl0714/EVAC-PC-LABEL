#pragma once
#include <windows.h>
#include <unknwn.h>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace rex::ui::d3d12 {
// Versioned NVAPI sleep ABI (NVIDIA/nvapi: nvapi.h, nvapi_interface.h).
class D3D12Reflex {
 public:
  void Connect(IUnknown* device, bool nvidia, bool requested);
  bool Pace(IUnknown* device);
  void Close(IUnknown* device);
  bool active() const { return active_.load(std::memory_order_acquire); }
 private:
  struct Mode {
    uint32_t version;
    uint8_t low_latency, boost;
    uint32_t minimum_interval_us;
    uint8_t markers, minimum_queue, reserved[30];
  };
  struct Status {
    uint32_t version;
    uint8_t low_latency, fullscreen_vrr, vsync_override;
    uint32_t sleep_interval_us;
    uint8_t game_sleep, independent_flip, generation_multiplier, dynamic_generation;
    uint32_t dynamic_generation_target_us;
    uint8_t reserved[114];
  };
  static_assert(sizeof(Mode) == 44 && sizeof(Status) == 136);
  using SetMode = int (__cdecl*)(IUnknown*, Mode*);
  using GetStatus = int (__cdecl*)(IUnknown*, Status*);
  using SleepProc = int (__cdecl*)(IUnknown*);
  using Lifecycle = int (__cdecl*)();
  bool Set(IUnknown* device, bool enabled);
  std::mutex mutex_;
  std::atomic<bool> active_{false};
  bool attempted_{}, initialized_{}, requested_mode_{};
  uint64_t calls_{};
  HMODULE module_{};
  SetMode set_{};
  GetStatus status_{};
  SleepProc sleep_{};
  Lifecycle unload_{};
};
}
