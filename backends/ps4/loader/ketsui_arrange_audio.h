#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cave::ps4 {
inline constexpr unsigned kKetsuiArrangeFirstPcm = 0x500;
inline constexpr unsigned kKetsuiArrangeCueCount = 13;
bool HasKetsuiArrangeAudio(std::string_view fingerprint);
std::vector<std::uint8_t> PrepareKetsuiArrangeAudio(
    std::string_view fingerprint, std::span<std::uint8_t> text, std::span<std::uint8_t> attributes,
    std::span<const std::uint8_t> data, std::uint64_t extension_address);
}
