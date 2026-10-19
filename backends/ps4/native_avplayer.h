#pragma once
#include <cstdint>
#include <string_view>
#include <filesystem>

enum class AvPlayerModuleAction { Load, Unload, IsLoaded };
int AvPlayerModule(AvPlayerModuleAction action);
std::uint64_t ResolveAvPlayerImport(std::string_view identifier, std::string_view library, std::string_view module);
bool AvPlayerMediaSelfTest(const std::filesystem::path& app, const std::filesystem::path& patch);
