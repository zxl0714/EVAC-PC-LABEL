#pragma once
#include <rex/ppc.h>
#include <rex/input/input_driver.h>
#include <cstdint>

using FutariProbeRecordFn = std::uint64_t (*)(std::uint32_t, std::uint64_t,
    std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
#ifdef EVAC_LATENCY_HOST
extern "C" __declspec(dllexport) std::uint64_t EvacLatencyRecord(std::uint32_t,
    std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
inline auto FutariProbeRecord = &EvacLatencyRecord;
#else
#include <Windows.h>
inline std::uint64_t FutariProbeRecord(std::uint32_t kind, std::uint64_t a,
    std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e, std::uint64_t f) {
  static const auto function = reinterpret_cast<FutariProbeRecordFn>(
      GetProcAddress(GetModuleHandleW(nullptr), "EvacLatencyRecord"));
  return function ? function(kind, a, b, c, d, e, f) : 0;
}
#endif
void FutariProbeInitialize();
void FutariProbeFlush();
void FutariProbeSwap(std::uint32_t buffer);
bool FutariProbeInput(rex::input::X_INPUT_STATE* output);
