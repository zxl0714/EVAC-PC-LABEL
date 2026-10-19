#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kt::audio {
struct VagInfo { unsigned channels{}, rate{}, data_offset{}, data_bytes{}, samples{}; };
struct VagFailure { const char* reason; bool unsupported{}; };
// Finite whole-file Vita/PS4 VAG profile. Loop markers are rejected until the
// block/decoder-history contract for those containers is implemented.
VagInfo ParseVag(std::span<const std::byte> file);
bool DecodeHevagFrame(std::span<const std::byte> frame, std::array<std::int32_t, 4>& history,
                     std::span<std::int16_t> output);
std::vector<std::byte> DecodeHevag(std::span<const std::byte> frames, unsigned channels);
bool HevagSelfTest();
}
