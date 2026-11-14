#pragma once
#include "preparation_io.h"
#include <nlohmann/json.hpp>

namespace cave {
class VerificationCache;
namespace ps4 {
// Empty when the optional soundtrack has not been installed.
nlohmann::json FindKetsuiArrangeTracks(const fs::path& data, VerificationCache& verification);
PreparationBytes DecodeKetsuiArrangeTrack(const fs::path& data, unsigned cue, std::string_view expected_hash);
}
}
