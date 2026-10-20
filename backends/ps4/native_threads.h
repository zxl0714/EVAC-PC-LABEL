#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveThreadImport(std::string_view identifier);
bool ThreadSelfTest();
bool ThreadAffinitySelfTest();
int NativeGuestThreadPriority();
// Retired hard-pinning map for authored A/B controls only; normal guest threads
// retain their inherited host eligible set, independently of guest CPU metadata.
std::uint64_t NativeHostAffinityForDiagnostics(std::uint64_t guest);
