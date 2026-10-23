#pragma once
#include <windows.h>
#include <cstdint>
#include "monotonic_clock.h"

namespace kt::diagnostics {
// Startup-only, opt-in diagnostic session. No persisted player setting.
inline bool TimingTraceEnabled() noexcept {
    static const bool enabled = [] {
        const auto error = GetLastError();
        wchar_t value[8]{};
        const bool result = GetEnvironmentVariableW(L"EVAC_PS4_TIMING_TRACE", value, 8) == 1 && value[0] == L'1';
        SetLastError(error); return result;
    }();
    return enabled;
}
inline std::uint64_t ThreadCpu100ns() noexcept {
    const auto error = GetLastError();
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool okay = GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != FALSE;
    SetLastError(error);
    return okay ? (std::uint64_t(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
           (std::uint64_t(user.dwHighDateTime) << 32 | user.dwLowDateTime) : 0;
}
}
