#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace cave {
// Shared radial deadzone; console-specific axis encoding happens afterwards.
inline std::pair<std::int16_t, std::int16_t> ApplyStickDeadzone(
    std::int16_t x, std::int16_t y, double deadzone) {
    const double nx = x / (x < 0 ? 32768.0 : 32767.0), ny = y / (y < 0 ? 32768.0 : 32767.0);
    const auto magnitude = std::hypot(nx, ny);
    if (magnitude <= deadzone || !magnitude) return {};
    const auto scale = (std::min(magnitude, 1.0) - deadzone) / ((1.0 - deadzone) * magnitude);
    auto convert = [](double value) {
        return static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0, 1.0) * (value < 0 ? 32768.0 : 32767.0)));
    };
    return {convert(nx * scale), convert(ny * scale)};
}
}
