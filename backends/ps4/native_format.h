#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveFormatImport(std::string_view identifier);
bool FormatSelfTest();
bool ScanSelfTest();
