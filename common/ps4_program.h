#pragma once
#include "common/paths.h"
#include <cstdint>
#include <span>
#include <vector>

namespace cave::ps4 {
// Decode an uncompressed plaintext SELF. ELF inputs are copied as-is.
std::vector<std::uint8_t> PlainSelfElf(std::span<const std::uint8_t> source);
// Hash the loadable program independently of its outer wrapper, section table
// and SDK version markers. Malformed or encrypted inputs have no fingerprint.
std::string ProgramFingerprint(std::span<const std::uint8_t> source);
std::string ProgramFingerprint(const fs::path& path);
}
