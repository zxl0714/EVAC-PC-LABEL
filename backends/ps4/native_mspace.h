#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveMspaceImport(std::string_view identifier);
bool MspaceSelfTest();
