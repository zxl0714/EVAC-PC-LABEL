#pragma once
#include "../guest_layout.h"
#include <rex/ppc/context.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace sdoj::latency {
// Only these nonvolatile values cross from the native input prefix into the parser.
// The TU uses r22 for the renderer and r29 for zero in both overlays.
struct NativePrefixResult {
    std::array<PPCRegister, 5> live{};
    std::array<std::uint8_t, 7> ports{};
    bool captured{};
    template<unsigned Module> void Capture(const PPCContext& ctx, const std::uint8_t* base) {
        static_assert(Module==1 || Module==2);
        if constexpr (kTitleUpdate) live={ctx.r19,ctx.r20,ctx.r21,ctx.r22,ctx.r29};
        else live={ctx.r20,ctx.r21,ctx.r22,ctx.r23,Module==1 ? ctx.r29 : ctx.r28};
        std::memcpy(ports.data(),base+live[3].u32+68,ports.size());
        captured=true;
    }
    template<unsigned Module> bool Restore(PPCContext& ctx, const std::uint8_t* base, std::uint32_t renderer) {
        static_assert(Module==1 || Module==2);
        if (!captured || live[3].u32!=renderer ||
            std::memcmp(ports.data(),base+renderer+68,ports.size())) return false;
        if constexpr (kTitleUpdate) {
            ctx.r19=live[0]; ctx.r20=live[1]; ctx.r21=live[2]; ctx.r22=live[3]; ctx.r29=live[4];
        } else {
            ctx.r20=live[0]; ctx.r21=live[1]; ctx.r22=live[2]; ctx.r23=live[3];
            if constexpr (Module==1) ctx.r29=live[4]; else ctx.r28=live[4];
        }
        captured=false;
        return true;
    }
};
}
