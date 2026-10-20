#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveSemaphoreImport(std::string_view identifier);
bool SemaphoreSelfTest();
// Diagnostic A/B selection, before the first guest semaphore is created.
bool ConfigureNativeSemaphoreAddressWait(bool enabled);
bool NativeSemaphoreAddressWaitEnabled();

// Host timestamps: the granting signal is only the final credit producer, not
// necessarily all producers of a batched request. CV resume includes gate
// reacquisition; successful address-waits do not. Wake timestamps can be zero
// when success is observed before the notifier calls/finishes Wake. Emission
// and guest-image lookup happen outside the semaphore gate.
struct NativeSemaphoreWaitTiming {
    std::uint64_t queued_clock_us{}, granted_clock_us{}, resumed_clock_us{};
    std::uint64_t wake_began_clock_us{}, wake_finished_clock_us{};
    bool address_wait{};
    unsigned grant_thread{};
    const void* grant_caller{};
};
bool EmitNativeSemaphoreWait(std::uint64_t handle, int requested, int result, const void* caller,
    const NativeSemaphoreWaitTiming& timing) noexcept;
