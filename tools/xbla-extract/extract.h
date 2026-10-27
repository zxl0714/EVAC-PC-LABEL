#pragma once
#include <filesystem>

namespace xblaextract {
// Verify the complete LIVE/PIRS package before creating a new output folder.
// Source data and existing destinations are never modified.
void Extract(const std::filesystem::path& input, const std::filesystem::path& output);
}
