#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>

std::uint64_t ResolveFileImport(std::string_view identifier, std::string_view library, std::string_view module);
void PrintNativeFileDiagnostics();
bool FileSelfTest(const std::filesystem::path& app, const std::filesystem::path& patch);
