#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace cave::ps4::cpu {
struct CodeRange {std::uint64_t address{},size{};};
// Use compiler unwind metadata to distinguish functions from embedded ROMs,
// tables and other data inside an executable PT_LOAD segment.
std::vector<CodeRange> FunctionRanges(std::span<const std::uint8_t> image,
                                    std::uint64_t header,std::uint64_t size);
}
