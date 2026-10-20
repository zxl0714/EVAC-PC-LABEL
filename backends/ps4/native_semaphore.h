#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveSemaphoreImport(std::string_view identifier);
bool SemaphoreSelfTest();

// Host timestamps: the granting signal is only the final credit producer, not
// necessarily all producers of a batched request. Resume includes reacquiring
// the semaphore gate. Emission/guest-image lookup happens after that gate exits.
struct NativeSemaphoreWaitTiming {
    std::uint64_t queued_clock_us{}, granted_clock_us{}, resumed_clock_us{};
    unsigned grant_thread{};
    const void* grant_caller{};
};
bool EmitNativeSemaphoreWait(std::uint64_t handle, int requested, int result, const void* caller,
    const NativeSemaphoreWaitTiming& timing) noexcept;
