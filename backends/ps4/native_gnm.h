#pragma once
#include <cstdint>
#include <string_view>
std::uint64_t ResolveGnmImport(std::string_view identifier, std::string_view library, std::string_view module);
// Authored PM4/direct-memory data, called only by the owned-window self-test.
bool GnmCloseSelfTest(int closed_video_handle);
