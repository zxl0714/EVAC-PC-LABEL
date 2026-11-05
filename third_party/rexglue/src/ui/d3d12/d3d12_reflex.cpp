#include <rex/ui/d3d12/d3d12_reflex.h>
#include <rex/logging.h>

namespace rex::ui::d3d12 {
bool D3D12Reflex::Set(IUnknown* device, bool enabled) {
  Mode mode{};
  mode.version = sizeof(mode) | (1u << 16);
  mode.low_latency = enabled;
  return set_(device, &mode) == 0;
}

void D3D12Reflex::Connect(IUnknown* device, bool nvidia, bool requested) {
  std::lock_guard lock(mutex_);
  if (attempted_) return;
  attempted_ = true;
  if (!requested || !nvidia) {
    REXLOG_INFO("REFLEX backend=d3d12 requested={} active=false reason={}", requested,
                nvidia ? "off" : "adapter");
    return;
  }
  module_ = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  using Query = void* (__cdecl*)(uint32_t);
  const auto query = module_ ? reinterpret_cast<Query>(GetProcAddress(module_, "nvapi_QueryInterface")) : nullptr;
  if (query) {
    const auto init = reinterpret_cast<Lifecycle>(query(0x0150E828));
    unload_ = reinterpret_cast<Lifecycle>(query(0xD22BDD7E));
    if (init && unload_ && init() == 0) {
      initialized_ = true;
      set_ = reinterpret_cast<SetMode>(query(0xAC1CA9E0));
      status_ = reinterpret_cast<GetStatus>(query(0xAEF96CA1));
      sleep_ = reinterpret_cast<SleepProc>(query(0x852CD1D2));
    }
  }
  requested_mode_ = set_ && status_ && sleep_ && Set(device, true);
  active_.store(requested_mode_, std::memory_order_release);
  REXLOG_INFO("REFLEX backend=d3d12 requested=auto status={}",
              requested_mode_ ? "awaiting_driver_readback" : "unsupported");
}

bool D3D12Reflex::Pace(IUnknown* device) {
  std::lock_guard lock(mutex_);
  if (!active()) return false;
  auto result = sleep_(device);
  ++calls_;
  if (result == 0 && (calls_ == 1 || calls_ == 120 || calls_ % 600 == 0)) {
    Status status{};
    status.version = sizeof(status) | (1u << 16);
    result = status_(device, &status);
    REXLOG_INFO("REFLEX backend=d3d12 sampled_frames={} status={} active={} game_sleep={} sleep_interval_us={}",
                calls_, result, status.low_latency != 0, status.game_sleep != 0, status.sleep_interval_us);
    if (result == 0 && !status.low_latency && calls_ >= 120) result = -1;
  }
  if (result == 0) return true;
  active_.store(false, std::memory_order_release);
  if (Set(device, false)) requested_mode_ = false;
  REXLOG_INFO("REFLEX backend=d3d12 active=false reason=driver_status status={}", result);
  return false;
}

void D3D12Reflex::Close(IUnknown* device) {
  std::lock_guard lock(mutex_);
  active_.store(false, std::memory_order_release);
  if (requested_mode_) Set(device, false);
  if (initialized_) unload_();
  if (module_) FreeLibrary(module_);
}
}
