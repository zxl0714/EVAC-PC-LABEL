#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace cave::ps2 {
// The last REGLIST has four UV/XYZ2 pairs. Coordinates use four fractional bits.
// A half-line downward raster displacement supplies the even rows of the output.
inline std::optional<unsigned> FieldBlitPhase(std::span<const uint8_t> packet,
                                              int offsetX,int offsetY,int height) {
    if(packet.size()<8*sizeof(uint64_t))return std::nullopt;
    std::array<uint64_t,8> uvXyz;
    std::memcpy(uvXyz.data(),packet.data()+packet.size()-sizeof(uvXyz),sizeof(uvXyz));
    auto x=[&](unsigned i){return int(uvXyz[2*i+1]&0xffff)-int(offsetX);};
    auto y=[&](unsigned i){return int((uvXyz[2*i+1]>>16)&0xffff)-int(offsetY);};
    const int top=y(0);
    // Ibara's final quad can end half a line early when its screen
    // coordinates are truncated. Both variants cover the last field row.
    const int bottomGap=16*height-(y(1)-top);
    if((top!=0 && top!=8) || (bottomGap!=0 && bottomGap!=8) ||
       y(2)!=y(1) || y(3)!=top || x(0)!=x(1) || x(2)!=x(3) || x(2)<=x(0) ||
       uvXyz[0]!=0 || uvXyz[2]==0 || uvXyz[6]==0 || (uvXyz[2]&0xffff) ||
       (uvXyz[6]>>16) || uvXyz[4]!=(uvXyz[2]|uvXyz[6]))return std::nullopt;
    return top==8?0u:1u;
}
}
