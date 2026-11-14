#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace cave::ps4 {
inline constexpr unsigned kKetsuiArrangeFirstPcm = 0x500;
inline constexpr unsigned kKetsuiArrangeCueCount = 13;
struct KetsuiAudioRelocation {
    std::uint64_t address, target;
};
std::vector<std::uint8_t> PrepareKetsuiArrangeAudio(
    std::span<std::uint8_t> text, std::span<std::uint8_t> attributes, std::uint64_t attributes_address,
    std::span<const std::uint8_t> data, std::uint64_t data_address,
    std::span<const KetsuiAudioRelocation> relative, std::uint64_t extension_address);
}
