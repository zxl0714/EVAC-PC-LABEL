#pragma once
#include <algorithm>
#include <cstdint>
#include <span>

namespace sdoj::compat {
inline constexpr std::uint32_t TilemapBytes=64*64*8;
// Only newly allocated normal-core maps. The native loader fills a scrolling
// rectangle, whereas its 11x9 draw window also reads the preceding column.
// Zero denotes an empty tile; untouched cells must not inherit heap contents.
inline bool InitializeTilemap(std::span<std::uint8_t> map) {
    if(map.size()!=TilemapBytes)return false;
    std::fill(map.begin(),map.end(),std::uint8_t{});
    return true;
}
}
