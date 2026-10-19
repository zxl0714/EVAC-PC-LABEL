#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>

enum class TrophyModuleAction { Load, Unload, IsLoaded };
int TrophyModule(TrophyModuleAction action);
std::uint64_t ResolveTrophyImport(std::string_view identifier, std::string_view library, std::string_view module);
bool TrophySelfTest(const std::filesystem::path& app, const std::filesystem::path& user, bool read_back);
