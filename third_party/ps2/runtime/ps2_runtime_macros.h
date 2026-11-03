#ifndef EVAC_PS2_RUNTIME_MACROS_H
#define EVAC_PS2_RUNTIME_MACROS_H

#include "ps2_upstream_runtime_macros.h"

// Share the address checks and wrapping accesses across generated instructions.
// Keep these helpers out of line: expanding them at every guest load/store
// duplicates substantially more native code than the operation itself.
// Constant-address FAST_READ/FAST_WRITE operations retain their inline path.
namespace cave::ps2 {
__declspec(noinline) inline uint8_t Read8(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load8(rdram, ctx, addr)
                                            : Ps2FastRead8(rdram, addr);
}
__declspec(noinline) inline uint16_t Read16(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load16(rdram, ctx, addr)
                                            : Ps2FastRead16(rdram, addr);
}
__declspec(noinline) inline uint32_t Read32(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load32(rdram, ctx, addr)
                                            : Ps2FastRead32(rdram, addr);
}
__declspec(noinline) inline uint64_t Read64(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load64(rdram, ctx, addr)
                                            : Ps2FastRead64(rdram, addr);
}
__declspec(noinline) inline __m128i Read128(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
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


namespace cave::ps2 {
__declspec(noinline) inline void Write8(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint8_t value) {
    if (PS2Runtime::isSpecialAddress(addr)) runtime->Store8(rdram, ctx, addr, value);
    else {
        ps2TraceGuestWrite(rdram, addr, 1u, value, 0u, "WRITE8", ctx);
        Ps2FastWrite8(rdram, addr, value);
    }
}
__declspec(noinline) inline void Write16(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint16_t value) {
    if (PS2Runtime::isSpecialAddress(addr)) runtime->Store16(rdram, ctx, addr, value);
    else {
        ps2TraceGuestWrite(rdram, addr, 2u, value, 0u, "WRITE16", ctx);
        Ps2FastWrite16(rdram, addr, value);
    }
}
__declspec(noinline) inline void Write32(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint32_t value) {
    if (PS2Runtime::isSpecialAddress(addr)) runtime->Store32(rdram, ctx, addr, value);
    else {
        ps2TraceGuestWrite(rdram, addr, 4u, value, 0u, "WRITE32", ctx);
        Ps2FastWrite32(rdram, addr, value);
    }
}
__declspec(noinline) inline void Write64(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint64_t value) {
    if (PS2Runtime::isSpecialAddress(addr)) runtime->Store64(rdram, ctx, addr, value);
    else {
        ps2TraceGuestWrite(rdram, addr, 8u, value, 0u, "WRITE64", ctx);
        Ps2FastWrite64(rdram, addr, value);
    }
}
__declspec(noinline) inline void Write128(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, __m128i value) {
    if (PS2Runtime::isSpecialAddress(addr)) runtime->Store128(rdram, ctx, addr, value);
    else {
        ps2TraceGuestWrite(rdram, addr, 16u, static_cast<uint64_t>(PS2_EXTRACT_EPI64_0(value)), static_cast<uint64_t>(PS2_EXTRACT_EPI64_1(value)), "WRITE128", ctx);
        Ps2FastWrite128(rdram, addr, value);
    }
}
}
#undef WRITE8
#define WRITE8(addr, val) ::cave::ps2::Write8(rdram, ctx, runtime, uint32_t(addr), (uint8_t)(val))
#undef WRITE16
#define WRITE16(addr, val) ::cave::ps2::Write16(rdram, ctx, runtime, uint32_t(addr), (uint16_t)(val))
#undef WRITE32
#define WRITE32(addr, val) ::cave::ps2::Write32(rdram, ctx, runtime, uint32_t(addr), (uint32_t)(val))
#undef WRITE64
#define WRITE64(addr, val) ::cave::ps2::Write64(rdram, ctx, runtime, uint32_t(addr), (uint64_t)(val))
#undef WRITE128
#define WRITE128(addr, val) ::cave::ps2::Write128(rdram, ctx, runtime, uint32_t(addr), (__m128i)(val))

#endif
