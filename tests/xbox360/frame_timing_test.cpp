#include <rex/graphics/frame_timing.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
int main() {
  try {
    rex::graphics::FrameTiming timing;
    Require(!timing.Observe(0), "Initial timestamp is not an interval");
    for (uint64_t i = 1; i < 120; ++i)
      Require(!timing.Observe(i * 16667), "Premature window");
    auto smooth = timing.Observe(120 * 16667);
    Require(smooth && smooth->intervals == 120, "Missing 60 Hz intervals");
    Require(std::abs(smooth->fps - 60) < .01 && smooth->over20 == 0, "Smooth cadence");
    Require(smooth->p99_upper_ms == 16.75, "Quantile upper bound");
    Require(smooth->maximum_end_us == 120 * 16667, "Peak timestamp uses latest tied interval");
    auto boundary = timing.Observe(120 * 16667 + 2000000);
    Require(boundary && boundary->intervals == 1 && boundary->over50 == 1,
            "Window boundary lost a long frame");
    Require(boundary->p95_upper_ms == 2000, "Overflow bucket lost the stall");
    Require(boundary->maximum_end_us == 120 * 16667 + 2000000, "Boundary peak timestamp lost");
    rex::graphics::FrameTiming jitter;
    jitter.Observe(0);
    for (uint64_t i = 1; i < 100; ++i) jitter.Observe(i * 10000, 1000000);
    auto stall = jitter.Observe(1100000, 1000000);
    Require(stall && stall->over20 == 1 && stall->maximum_ms == 110, "Long frame count");
    Require(stall->maximum_end_us == 1100000, "Long frame timestamp lost");
    Require(stall->p95_upper_ms == 10.25 && stall->p99_upper_ms == 10.25,
            "Quantile rank with a single outlier");
    Require(!jitter.Observe(10), "Clock reset created unsigned interval");
    auto reset = jitter.Observe(2000010);
    Require(reset && reset->intervals == 1 && reset->maximum_ms == 2000, "Clock reset state");
    Require(reset->maximum_end_us == 2000010, "Clock reset retained an old peak timestamp");
    std::cout << "Bounded frame timing passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
