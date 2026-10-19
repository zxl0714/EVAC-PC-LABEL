#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace kt::content {
struct ReadPatch {
    std::string path;
    std::uint64_t file_size{}, offset{};
    std::array<std::byte, 32> original_sha256{};
    std::vector<std::byte> replacement;
};
struct ReadPatchSet {
    std::string title, version;
    std::vector<ReadPatch> entries;
    static ReadPatchSet Load(const std::filesystem::path& path);
};
std::array<std::byte, 32> ReadPatchHash(std::span<const std::byte> bytes);
}
