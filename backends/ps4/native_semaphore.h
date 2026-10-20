#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveSemaphoreImport(std::string_view identifier);
bool SemaphoreSelfTest();
// Diagnostic selection, before the first guest semaphore is created.
enum class NativeSemaphoreWaitPrimitive { ConditionVariable, Address, Event };
bool ConfigureNativeSemaphoreWait(NativeSemaphoreWaitPrimitive primitive);
NativeSemaphoreWaitPrimitive NativeSemaphoreWaitMode();
const char* NativeSemaphoreWaitName();

// Host timestamps: the granting signal is only the final credit producer, not
// necessarily all producers of a batched request. CV resume includes gate
// reacquisition; successful address/event waits do not. Wake timestamps can be zero
// when success is observed before the notifier calls/finishes Wake. Emission
// and guest-image lookup happen outside the semaphore gate.
struct NativeSemaphoreWaitTiming {
    std::uint64_t wait_sequence{}, lookup_us{}, gate_wait_us{}, queued_under_gate_us{};
    std::uint64_t queued_clock_us{}, granted_clock_us{}, resumed_clock_us{};
    std::uint64_t wake_began_clock_us{}, wake_finished_clock_us{};
    // Waiter-side last blocking call, not the notifier-side Wake() interval.
    // CV return includes reacquisition; address/event return does not.
    std::uint64_t park_began_clock_us{}, park_returned_clock_us{}, park_calls{};
    unsigned queued_waiters{}, available_credits{};
    bool address_wait{};
    bool event_wait{};
    unsigned grant_thread{};
    const void* grant_caller{};
};
bool EmitNativeSemaphoreWait(std::uint64_t handle, int requested, int result, const void* caller,
    const NativeSemaphoreWaitTiming& timing) noexcept;
