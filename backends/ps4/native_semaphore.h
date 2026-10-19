#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveSemaphoreImport(std::string_view identifier);
bool SemaphoreSelfTest();
