#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveTextImport(std::string_view identifier);
bool TextSelfTest();
