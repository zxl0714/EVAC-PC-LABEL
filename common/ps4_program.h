#pragma once
#include "common/paths.h"
#include <cstdint>
#include <span>
#include <vector>

namespace cave::ps4 {
// Decode an uncompressed plaintext SELF. ELF inputs are copied as-is.
std::vector<std::uint8_t> PlainSelfElf(std::span<const std::uint8_t> source);
// The SHA-256 of the verified program view, independent of its outer wrapper
// and the ELF section table. Malformed or encrypted inputs have no fingerprint.
std::string ProgramFingerprint(std::span<const std::uint8_t> source);
std::string ProgramFingerprint(const fs::path& path);
}
