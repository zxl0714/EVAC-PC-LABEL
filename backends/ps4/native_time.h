#pragma once
#include <cstdint>
#include <string_view>

void InitializeNativeTime();
std::uint64_t NativeProcessMicroseconds();
std::uint64_t NativeHostTsc();
std::uint64_t ResolveTimeImport(std::string_view identifier);
bool TimeSelfTest();
