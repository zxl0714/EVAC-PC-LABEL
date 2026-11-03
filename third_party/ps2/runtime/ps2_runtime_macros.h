#ifndef EVAC_PS2_RUNTIME_MACROS_H
#define EVAC_PS2_RUNTIME_MACROS_H

#include "ps2_upstream_runtime_macros.h"

// Generated functions can contain thousands of loads. A capturing lambda at
// every load gives the compiler a distinct helper to emit when it stops
// inlining a large function. Named helpers let those calls share one body.
// Keep the upstream address dispatch, RAM access and load width unchanged.
namespace cave::ps2 {
inline uint8_t Read8(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load8(rdram, ctx, addr)
                                            : Ps2FastRead8(rdram, addr);
}
inline uint16_t Read16(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load16(rdram, ctx, addr)
                                            : Ps2FastRead16(rdram, addr);
}
inline uint32_t Read32(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load32(rdram, ctx, addr)
                                            : Ps2FastRead32(rdram, addr);
}
inline uint64_t Read64(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load64(rdram, ctx, addr)
                                            : Ps2FastRead64(rdram, addr);
}
inline __m128i Read128(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load128(rdram, ctx, addr)
                                            : Ps2FastRead128(rdram, addr);
}
}

#undef READ8
#undef READ16
#undef READ32
#undef READ64
#undef READ128
#define READ8(addr) ::cave::ps2::Read8(rdram, ctx, runtime, uint32_t(addr))
#define READ16(addr) ::cave::ps2::Read16(rdram, ctx, runtime, uint32_t(addr))
#define READ32(addr) ::cave::ps2::Read32(rdram, ctx, runtime, uint32_t(addr))
#define READ64(addr) ::cave::ps2::Read64(rdram, ctx, runtime, uint32_t(addr))
#define READ128(addr) ::cave::ps2::Read128(rdram, ctx, runtime, uint32_t(addr))

#endif
