#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace cave::ps4 {
struct PreparedElf {
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t,32> source_sha256;
    std::uint64_t image_size{}, imports{}, exports{}, relocations{}, segments{};
};
// Pure preparation: never maps executable memory, executes an input, accesses
// the network or changes a source. Output contains local derived game data.
PreparedElf PrepareElf(std::span<const std::uint8_t> source, bool module,
                       std::span<const std::uint8_t> libc = {});
int PrepareElfCommand(int argc, wchar_t** argv);
}
