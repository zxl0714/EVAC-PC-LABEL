#pragma once
#include <cstdint>
#include <windows.h>

namespace futari::performance {
// Wait on the original aligned guest word, in its stored byte order. The
// original guest load/compare/branch still runs after this returns, including
// spurious wakes. No counter, deadline or game state is synthesized here.
inline bool WaitCounter(void* address, std::uint64_t stored_expected,
                        DWORD timeout_ms = 50) {
    return WaitOnAddress(address, &stored_expected, sizeof(stored_expected), timeout_ms) != FALSE;
}
inline void NotifyCounter(void* address) { WakeByAddressAll(address); }
void StartCounterWait();
void StopCounterWait();
}
