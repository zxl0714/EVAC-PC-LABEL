#pragma once
#include <cstdint>
#include <string_view>
std::uint64_t ResolveMathImport(std::string_view identifier);
bool MathSelfTest();
