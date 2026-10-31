#pragma once
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string_view>

namespace cave::ps2 {
struct IsoFile {uint32_t lba{},size{};};
std::optional<IsoFile> FindIsoFile(std::istream& image,uint64_t imageSize,std::string_view path);
}
