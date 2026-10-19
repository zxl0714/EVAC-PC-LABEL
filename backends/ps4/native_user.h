#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveUserImport(std::string_view identifier, std::string_view library,
                                std::string_view module);
bool UserSelfTest();
bool NativeUserInitialized();
