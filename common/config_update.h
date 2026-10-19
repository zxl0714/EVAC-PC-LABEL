#pragma once
#include "common/ini.h"
#include <span>

namespace cave {
struct IniEdit { std::string section, key, value; };
// Preserve all other text, including BOM, comments, case and line endings.
std::string EditIni(std::string_view before, std::span<const IniEdit> changes);
// Refuse an editor's intervening changes; replace the complete file atomically.
void UpdateConfig(const fs::path& root, std::string_view before, std::span<const IniEdit> changes);
}  // namespace cave
