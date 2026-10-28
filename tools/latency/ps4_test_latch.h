#pragma once
#include <cstdint>

namespace evac_latency {
// Measurement-only equivalent of the native input pending-down latch. The
// caller owns the pad mutex. This is not a Windows/SDL/physical input source.
struct Ps4TestLatch {
  std::uint64_t seen{}, held_source{}, pending_source{};
  std::uint32_t held{}, pending{}, pending_report{};

  void Update(std::uint64_t event, std::uint32_t buttons) {
    if (event == seen) return;
    if (buttons & ~held) {
      pending |= buttons & ~held;
      pending_source = event;
      pending_report = buttons;
    }
    seen = held_source = event;
    held = buttons;
  }
  std::uint32_t Buttons() const { return held | pending; }
  std::uint64_t Source() const {
    // Overlapping unconsumed commands can combine states from different events.
    // Leave those unpaired rather than attributing a mixed report to one event.
    if (pending) return Buttons() == pending_report ? pending_source : 0;
    return held_source;
  }
  void Acknowledge() { pending = 0; pending_source = 0; pending_report = 0; }
};
} // namespace evac_latency
