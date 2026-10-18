// Bounded, single-producer cadence statistics. No frame samples or I/O queue.
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace rex::graphics {
struct FrameTimingSummary {
  uint64_t intervals = 0;
  uint64_t maximum_end_us = 0;
  double elapsed_ms = 0, fps = 0, mean_ms = 0, minimum_ms = 0, maximum_ms = 0;
  double p95_upper_ms = 0, p99_upper_ms = 0;
  uint64_t over20 = 0, over33 = 0, over50 = 0;
};

class FrameTiming {
 public:
  static uint64_t NowMicros() {
    return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  // Histogram quantiles are conservative upper bounds, in 0.25 ms bins.
  // The last bin includes long stalls and uses the observed maximum.
  std::optional<FrameTimingSummary> Observe(uint64_t now_us,
                                           uint64_t window_us = 2000000) {
    if (!initialized_ || now_us < last_us_) {
      *this = FrameTiming{};
      initialized_ = true;
      first_us_ = last_us_ = now_us;
      return std::nullopt;
    }
    const auto delta = now_us - last_us_;
    last_us_ = now_us;
    ++count_;
    minimum_us_ = std::min(minimum_us_, delta);
    if (delta >= maximum_us_) {
      maximum_us_ = delta;
      maximum_end_us_ = now_us;
    }
    ++bins_[std::min<uint64_t>(delta / 250, bins_.size() - 1)];
    over20_ += delta > 20000;
    over33_ += delta > 33333;
    over50_ += delta > 50000;
    const auto elapsed = now_us - first_us_;
    if (elapsed < window_us || !elapsed) return std::nullopt;
    FrameTimingSummary result;
    result.intervals = count_;
    result.elapsed_ms = double(elapsed) / 1000;
    result.fps = double(count_) * 1000000 / double(elapsed);
    result.mean_ms = double(elapsed) / double(count_) / 1000;
    result.minimum_ms = double(minimum_us_) / 1000;
    result.maximum_ms = double(maximum_us_) / 1000;
    result.maximum_end_us = maximum_end_us_;
    result.p95_upper_ms = Quantile((count_ * 95 + 99) / 100);
    result.p99_upper_ms = Quantile((count_ * 99 + 99) / 100);
    result.over20 = over20_; result.over33 = over33_; result.over50 = over50_;
    // Preserve the boundary timestamp: no inter-window gap is discarded.
    *this = FrameTiming{};
    initialized_ = true;
    first_us_ = last_us_ = now_us;
    return result;
  }
 private:
  double Quantile(uint64_t target) const {
    uint64_t cumulative = 0;
    for (size_t i = 0; i < bins_.size(); ++i) {
      cumulative += bins_[i];
      if (cumulative >= target)
        return i + 1 == bins_.size() ? double(maximum_us_) / 1000
                                     : double((i + 1) * 250) / 1000;
    }
    return double(maximum_us_) / 1000;
  }
  std::array<uint64_t, 512> bins_{};
  bool initialized_ = false;
  uint64_t first_us_ = 0, last_us_ = 0, count_ = 0;
  uint64_t minimum_us_ = UINT64_MAX, maximum_us_ = 0;
  uint64_t maximum_end_us_ = 0;
  uint64_t over20_ = 0, over33_ = 0, over50_ = 0;
};
}  // namespace rex::graphics
