#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveThreadImport(std::string_view identifier);
bool ThreadSelfTest();
bool ThreadAffinitySelfTest();
int NativeGuestThreadPriority();
