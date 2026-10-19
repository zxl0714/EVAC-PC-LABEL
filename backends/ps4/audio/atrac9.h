#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kt::audio {
struct Atrac9Failure { const char* reason; bool unsupported{}; };
struct Atrac9Config {
    unsigned channels{}, rate{}, superframe_bytes{}, frame_samples{}, frames_per_superframe{};
    unsigned SuperframeSamples() const { return frame_samples * frames_per_superframe; }
};
struct Atrac9Wave {
    Atrac9Config codec;
    std::uint32_t config{};
    unsigned data_offset{}, data_bytes{}, samples{}, delay{};
    unsigned loop_start{}, loop_end{}, loop_count{}; // Absolute decoded frames; end exclusive.
    bool looping{};
};
// Config stores the four stream bytes in little-endian memory order, not a Sony enum.
Atrac9Config InspectAtrac9Config(std::uint32_t config);
Atrac9Wave ParseAtrac9Wave(std::span<const std::byte> file);
std::vector<std::byte> DecodeAtrac9(std::span<const std::byte> data, std::uint32_t config);
bool Atrac9SelfTest();
}
