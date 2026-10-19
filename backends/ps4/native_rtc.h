#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveRtcImport(std::string_view identifier, std::string_view library, std::string_view module);
bool RtcSelfTest();

// Shared PC wall-clock/time-zone backend for libc's seconds-based calendar.
bool NativeUtcClockSeconds(std::int64_t& output);
bool NativeUtcToLocalSeconds(std::int64_t utc, std::int64_t& local, int& daylight);
bool NativeLocalToUtcSeconds(std::int64_t local, int daylight, std::int64_t& utc,
                            std::int64_t& normalized_local, int& normalized_daylight);
