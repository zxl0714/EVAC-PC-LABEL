// Compare actual production FrameTiming calls against bracketing QPC samples.
// This checks an existing clock mapping; it does not estimate/fix any offset.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <rex/graphics/frame_timing.h>
#include <cstdio>
#include <cstdint>

int main() {
  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  unsigned failures = 0;
  std::uint64_t maximum_bracket = 0;
  constexpr unsigned samples = 1000;
  for (unsigned i = 0; i < samples; ++i) {
    LARGE_INTEGER before{}, after{};
    QueryPerformanceCounter(&before);
    const auto micros = rex::graphics::FrameTiming::NowMicros();
    QueryPerformanceCounter(&after);
    const auto to_micros = [&](std::uint64_t qpc) {
      const auto hz = std::uint64_t(frequency.QuadPart);
      return (qpc / hz) * 1000000 + ((qpc % hz) * 1000000) / hz;
    };
    const auto lo = to_micros(before.QuadPart), hi = to_micros(after.QuadPart);
    if (micros < lo || micros > hi) ++failures;
    maximum_bracket = (std::max)(maximum_bracket, hi - lo);
  }
  std::printf("{\"valid\":%s,\"samples\":%u,\"frequency\":%lld,\"failures\":%u,"
      "\"maximum_bracket_us\":%llu,\"mapping\":\"floor(qpc*1000000/frequency)\"}\n",
      failures ? "false" : "true", samples, frequency.QuadPart, failures,
      static_cast<unsigned long long>(maximum_bracket));
  return failures ? 1 : 0;
}
