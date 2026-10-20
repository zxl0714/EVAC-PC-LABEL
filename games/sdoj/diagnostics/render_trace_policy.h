#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace sdoj::diagnostics {
// Disc-base 88033070/880331F0, independently reviewed in each overlapping module.
// countdown, swap serial, active writer, published buffer, published status.
constexpr std::array<std::uint32_t, 5> RenderAddresses(unsigned module) {
    if (module == 1) return {0x88879184, 0x886F4CD0, 0x886F4D10, 0x888791A4, 0x888791A8};
    if (module == 2) return {0x888816AC, 0x886FD1F8, 0x886FD238, 0x888816CC, 0x888816D0};
    return {};
}
inline std::optional<std::uint32_t> RenderWord(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 4) return {};
    return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
        (std::uint32_t(bytes[2]) << 8) | bytes[3];
}
struct RenderWait {
    bool active{};
    std::uint32_t depth{};
    std::uint64_t generation{}, us{}, frame{};
    bool Matches(std::uint64_t current_generation, std::uint64_t now,
                 std::uint64_t current_frame) const {
        return active && depth && depth <= 16 && generation == current_generation &&
            now >= us && current_frame >= frame;
    }
};
}
