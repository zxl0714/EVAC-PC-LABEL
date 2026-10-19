#pragma once

#include <cstdint>
#include <string_view>

std::uint64_t ResolveMutexImport(std::string_view identifier);
bool MutexSelfTest();
bool ConditionSelfTest();
