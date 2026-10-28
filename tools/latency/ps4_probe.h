#pragma once

// Private measurement build only. These records do not assert that a PadRead
// produced the next submitted image. That link needs game/pixel evidence.
#include "test_pad.h"

namespace evac_latency {
constexpr uint32_t Ps4Buttons(uint16_t xbox) {
  uint32_t result = 0;
  constexpr uint32_t mapping[16] = {
      0x10, 0x40, 0x80, 0x20, 0x08, 0x100000, 0x02, 0x04,
      0x400, 0x800, 0, 0, 0x4000, 0x2000, 0x8000, 0x1000};
  for (unsigned bit = 0; bit < 16; ++bit)
    if (xbox & (1u << bit)) result |= mapping[bit];
  return result;
}

struct Ps4PresentContext {
  uint64_t ready = 0;
  bool game = false;
  bool reuse = false;
};
inline thread_local Ps4PresentContext ps4_present;
inline thread_local uint64_t ps4_sdl_event = 0;
struct Ps4SdlScope {
  uint64_t previous;
  explicit Ps4SdlScope(uint64_t event) : previous(ps4_sdl_event) { ps4_sdl_event = event; }
  ~Ps4SdlScope() { ps4_sdl_event = previous; }
};
struct Ps4PresentScope {
  Ps4PresentContext previous;
  Ps4PresentScope(uint64_t ready, bool game, bool reuse) : previous(ps4_present) {
    ps4_present = {ready, game, reuse};
  }
  ~Ps4PresentScope() { ps4_present = previous; }
};
} // namespace evac_latency
