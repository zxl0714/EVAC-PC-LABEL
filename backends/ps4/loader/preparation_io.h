#pragma once
#include "common/paths.h"
#include <cstdint>
#include <span>
#include <vector>

namespace cave::ps4 {
using PreparationBytes = std::vector<std::uint8_t>;
PreparationBytes ReadPreparationInput(const fs::path& path,std::uint64_t limit=512ull*1024*1024);
std::string PreparationHash(std::span<const std::uint8_t> bytes);
void CheckPreparationPath(const fs::path& root,const fs::path& literal,
                          std::span<const fs::path> source_directories={},bool require_new=false,
                          bool allow_container=false);
void WritePreparationFile(const fs::path& path,std::span<const std::uint8_t> bytes);
}
