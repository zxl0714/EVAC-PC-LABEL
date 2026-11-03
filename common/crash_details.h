#pragma once
#include <filesystem>
#include <string>

namespace cave {
// Reads bounded metadata from an existing dump; never loads its images/code.
// Throws on malformed input. The upload caller preserves other logs on failure.
std::string SummarizeCrashDump(const std::filesystem::path& path);
}
