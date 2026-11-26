#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveUserImport(std::string_view identifier, std::string_view library,
                                std::string_view module);
bool UserSelfTest();
bool NativeUserInitialized();
inline constexpr int kNativePrimaryUser = 0x1000;
inline constexpr int kNativeSecondUser = kNativePrimaryUser + 1;
void ConfigureNativeSecondUser(bool enabled);
