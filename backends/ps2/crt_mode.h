#pragma once
#include <cstdint>
#include <stdexcept>

namespace cave::ps2 {
// PS2 BIOS video-mode IDs, not the host window or a per-game resolution override.
// Stable PLL/carrier fields follow ps2sdk's graph_mode.c. Hardware PLL settling
// is not a host wait; the display registers remain owned by the guest.
inline uint64_t CrtSmode1(uint32_t mode) {
    constexpr uint64_t clock=(1ull<<30)|(1ull<<32)|(1ull<<33);
    switch(mode) {
    case 2: return clock|(1ull<<34)|4|(32ull<<3)|(1ull<<10)|(2ull<<13)|(4ull<<21);
    case 3: return clock|(1ull<<34)|4|(32ull<<3)|(1ull<<10)|(3ull<<13)|(4ull<<21);
    case 0x50: return clock|(1ull<<34)|(1ull<<36)|4|(32ull<<3)|(1ull<<10)|(2ull<<21);
    case 0x53: return clock|(1ull<<34)|(1ull<<36)|4|(32ull<<3)|(1ull<<10)|(1ull<<19)|(2ull<<21);
    case 0x51: return clock|2|(22ull<<3)|(1ull<<10)|(1ull<<21);
    case 0x52: return clock|(1ull<<36)|2|(22ull<<3)|(1ull<<10)|(1ull<<21);
    case 0x1a: return clock|(1ull<<36)|2|(15ull<<3)|(1ull<<10)|(2ull<<21);
    default: throw std::invalid_argument("Unsupported PS2 CRT mode");
    }
}
inline uint64_t CrtSmode2(bool interlace,bool frameMode) {
    return uint64_t(interlace)|(uint64_t(frameMode)<<1);
}
struct SdkDisplay {uint64_t pmode,smode2,dispfb,display,bgcolor;};
// libgraph display-builder semantics: caller offsets are signed pixels, DX/DW
// are video-clock units, and frame-mode height spans both interlaced fields.
inline SdkDisplay MakeSdkDisplay(uint32_t mode,bool interlace,bool frameMode,
                                 uint32_t psm,uint32_t width,uint32_t height,
                                 int32_t x=0,int32_t y=0) {
    if(!width || !height || width>4096 || height>2048)
        throw std::invalid_argument("Invalid PS2 display dimensions");
    int32_t scale,dx,dy;uint32_t dh;
    if(mode==2 || mode==3) {
        scale=int32_t((2560+width-1)/width);
        dx=(mode==2?636:656)+x*scale;
        dy=(mode==2?25:36)*(interlace?2:1)+y;
        dh=height*(interlace && frameMode?2:1);
    }else if(mode==0x50) {
        scale=2;dx=232+((720-int32_t(width))/2)*2+x*2;dy=35+y;dh=height;
    }else throw std::invalid_argument("Unsupported PS2 SDK display mode");
    if(scale<1 || scale>16)throw std::invalid_argument("Invalid PS2 display magnification");
    const uint64_t display=(uint64_t(dx)&0xfff)|((uint64_t(dy)&0x7ff)<<12)|
        (uint64_t(scale-1)<<23)|((uint64_t(width*scale-1)&0xfff)<<32)|
        ((uint64_t(dh-1)&0x7ff)<<44);
    return {0x66,interlace?(frameMode?3ull:1ull):2ull,
        (uint64_t((width+63)/64)&63)<<9|((uint64_t(psm)&15)<<15),display,0};
}
}
