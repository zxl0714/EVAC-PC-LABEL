#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
inline std::uint64_t Ps4ProbeHostRecord(std::uint32_t kind, std::uint64_t a = 0,
    std::uint64_t b = 0, std::uint64_t c = 0, std::uint64_t d = 0,
    std::uint64_t e = 0, std::uint64_t f = 0) {
  using Function = std::uint64_t (*)(std::uint32_t, std::uint64_t, std::uint64_t,
      std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
  static const auto function = reinterpret_cast<Function>(
      GetProcAddress(GetModuleHandleW(nullptr), "EvacLatencyRecord"));
  return function ? function(kind, a, b, c, d, e, f) : 0;
}
