#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveCxxGuardImport(std::string_view identifier);
bool CxxGuardSelfTest();
