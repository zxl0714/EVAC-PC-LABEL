// Copyright 2026 EVAC PC LABEL contributors. BSD-3-Clause.
#pragma once
#include <cstdint>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rex::thread {
// Both endpoints must use Memory::TranslatePhysical, not different virtual
// aliases of the same guest page. The producer writes first, then notifies.
// A wake is only a reason to re-run the original guest predicate.
inline bool WaitGuestMemoryWord(void* canonical_word, std::uint32_t stored_value,
                                std::uint32_t timeout_ms = 50) {
#if defined(_WIN32)
  return WaitOnAddress(canonical_word, &stored_value, sizeof(stored_value), timeout_ms) != FALSE;
#else
  return false;
#endif
}
inline void NotifyGuestMemoryWord(void* canonical_word) {
#if defined(_WIN32)
  WakeByAddressAll(canonical_word);
#endif
}
}  // namespace rex::thread
