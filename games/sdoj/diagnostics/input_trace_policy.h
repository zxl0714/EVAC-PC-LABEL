#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sdoj::diagnostics {
struct PadSample {
    std::uint32_t packet{};
    std::uint16_t buttons{};
    std::uint8_t left_trigger{}, right_trigger{};
    std::array<std::uint16_t, 4> axes{};
    bool operator==(const PadSample&) const = default;
    bool SameControls(const PadSample& other) const {
        return buttons == other.buttons && left_trigger == other.left_trigger &&
            right_trigger == other.right_trigger && axes == other.axes;
    }
};

// X_INPUT_STATE is big-endian guest data, not a native XInput structure.
inline std::optional<PadSample> DecodePad(std::span<const std::uint8_t> bytes,
                                          std::uint32_t status) {
    if (status || bytes.size() != 16) return {};
    auto u16 = [&](std::size_t i) {
        return std::uint16_t((std::uint16_t(bytes[i]) << 8) | bytes[i + 1]);
    };
    PadSample out;
    out.packet = (std::uint32_t(u16(0)) << 16) | u16(2);
    out.buttons = u16(4);
    out.left_trigger = bytes[6]; out.right_trigger = bytes[7];
    for (std::size_t i = 0; i != 4; ++i) out.axes[i] = u16(8 + 2 * i);
    return out;
}

struct InputPass {
    bool active{}, changed{};
    std::uint32_t object{}, polls{};
    std::uint64_t began_us{}, began_frame{};
    void Begin(std::uint32_t owner, std::uint64_t us, std::uint64_t frame) {
        active = true; changed = false; object = owner; polls = 0;
        began_us = us; began_frame = frame;
    }
    bool MatchesEnd(std::uint32_t manager, std::uint64_t us) const {
        // Reviewed disc-base engine object -> input manager relationship.
        return active && object && std::uint64_t(object) + 20100 == manager && us >= began_us;
    }
};
}
