#pragma once
#include <chrono>
#include <cstdint>

namespace kt::diagnostics {
// The same steady-clock epoch as native_gnm_frame_completed.completed_clock_us.
// This is a host timeline, not the guest clock or physical speaker/scanout time.
inline std::uint64_t MonotonicMicroseconds() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}
