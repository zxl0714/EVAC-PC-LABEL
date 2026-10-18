// Guest video cadence is independent of host presentation synchronization.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rex::graphics {
inline uint64_t GuestVblankHostWaitMicros(uint64_t remaining_ticks,
                                        uint64_t frequency, double time_scalar) {
  if (!remaining_ticks) return 0;
  if (!frequency || !std::isfinite(time_scalar) || time_scalar <= 0) return 1000;
  const long double micros = static_cast<long double>(remaining_ticks) * 1000000 /
                             frequency / time_scalar;
  // Keep shutdown and clock-setting changes responsive, even at low speed.
  if (micros >= 100000) return 100000;
  return static_cast<uint64_t>(std::max(0.L, micros));
}
inline uint64_t GuestVblankIntervalTicks(uint64_t frequency, double refresh_hz) {
  if (!std::isfinite(refresh_hz) || refresh_hz < 1.0) {
    refresh_hz = 1.0;
  }
  const auto ticks = static_cast<long double>(frequency) / refresh_hz;
  if (ticks >= static_cast<long double>(std::numeric_limits<uint64_t>::max())) {
    return std::numeric_limits<uint64_t>::max();
  }
  return std::max(uint64_t(1), static_cast<uint64_t>(ticks));
}
}  // namespace rex::graphics
