#pragma once
// Local audio-r1 instrumentation. Never records PCM or changes playback policy.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace rex::audio::diag {
inline bool Enabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("REX_AUDIO_DIAGNOSTICS");
    return !value || std::strcmp(value, "0") != 0;
  }();
  return enabled;
}
inline uint64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void Max(std::atomic<uint64_t>& target, uint64_t value) {
  auto old = target.load(std::memory_order_relaxed);
  while (value > old && !target.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
}
struct Counters {
  std::atomic<uint64_t> guest_callbacks{0}, guest_busy_us{0}, guest_max_us{0};
  std::atomic<uint64_t> guest_started_us{0}, guest_finished_us{0}, guest_callback{0};
  std::atomic<uint64_t> xma_work{0}, xma_busy_us{0}, xma_max_us{0}, xma_slow{0};
  std::atomic<uint64_t> xma_started_us{0}, xma_finished_us{0}, xma_context{0};
  std::atomic<uint64_t> xma_active{0};
  std::atomic<uint64_t> xma_resets{0}, xma_releases{0}, xma_kicks{0};
};
inline Counters counters;
class XmaWorkTimer {
 public:
  explicit XmaWorkTimer(uint32_t context) : start_(Enabled() ? NowUs() : 0) {
    if (!start_) return;
    counters.xma_context.store(context, std::memory_order_relaxed);
    if (counters.xma_active.fetch_add(1, std::memory_order_relaxed) == 0)
      counters.xma_started_us.store(start_, std::memory_order_relaxed);
  }
  ~XmaWorkTimer() {
    if (!start_) return;
    const auto end = NowUs(), elapsed = end - start_;
    counters.xma_work.fetch_add(1, std::memory_order_relaxed);
    counters.xma_busy_us.fetch_add(elapsed, std::memory_order_relaxed);
    Max(counters.xma_max_us, elapsed);
    if (elapsed >= 20000) counters.xma_slow.fetch_add(1, std::memory_order_relaxed);
    if (counters.xma_active.fetch_sub(1, std::memory_order_relaxed) == 1)
      counters.xma_finished_us.store(end, std::memory_order_relaxed);
  }
 private:
  uint64_t start_;
};
}  // namespace rex::audio::diag
