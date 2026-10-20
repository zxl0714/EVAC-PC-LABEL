#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveThreadImport(std::string_view identifier);
bool ThreadSelfTest();
bool ThreadAffinitySelfTest();
int NativeGuestThreadPriority();
// Exposes the production mapping to authored benchmarks, without changing it.
std::uint64_t NativeHostAffinityForDiagnostics(std::uint64_t guest);
