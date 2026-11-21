#ifndef EVAC_PS2_RUNTIME_MACROS_H
#define EVAC_PS2_RUNTIME_MACROS_H

#include "ps2_upstream_runtime_macros.h"

// Share the address checks and wrapping accesses across generated instructions.
// Keep these helpers out of line: expanding them at every guest load/store
// duplicates substantially more native code than the operation itself.
// Constant-address FAST_READ/FAST_WRITE operations retain their inline path.
namespace cave::ps2::detail {
__forceinline uint8_t Read8(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load8(rdram, ctx, addr)
                                            : Ps2FastRead8(rdram, addr);
}
__forceinline uint16_t Read16(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load16(rdram, ctx, addr)
                                            : Ps2FastRead16(rdram, addr);
}
__forceinline uint32_t Read32(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load32(rdram, ctx, addr)
                                            : Ps2FastRead32(rdram, addr);
}
__forceinline uint64_t Read64(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load64(rdram, ctx, addr)
                                            : Ps2FastRead64(rdram, addr);
}
__forceinline __m128i Read128(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return PS2Runtime::isSpecialAddress(addr) ? runtime->Load128(rdram, ctx, addr)
                                            : Ps2FastRead128(rdram, addr);
}
}

namespace cave::ps2 {
__declspec(noinline) inline uint8_t Read8(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return detail::Read8(rdram, ctx, runtime, addr);
}
__declspec(noinline) inline uint16_t Read16(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return detail::Read16(rdram, ctx, runtime, addr);
}
__declspec(noinline) inline uint32_t Read32(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return detail::Read32(rdram, ctx, runtime, addr);
}
__declspec(noinline) inline uint64_t Read64(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return detail::Read64(rdram, ctx, runtime, addr);
}
__declspec(noinline) inline __m128i Read128(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr) {
    return detail::Read128(rdram, ctx, runtime, addr);
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


namespace cave::ps2::detail {
__forceinline void Write8(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint8_t value) {
    if (PS2Runtime::isSpecialAddress(addr)) runtime->Store8(rdram, ctx, addr, value);
    else {
        ps2TraceGuestWrite(rdram, addr, 1u, value, 0u, "WRITE8", ctx);
        Ps2FastWrite8(rdram, addr, value);
    }
}
__forceinline void Write16(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint16_t value) {
    if (PS2Runtime::isSpecialAddress(addr)) runtime->Store16(rdram, ctx, addr, value);
    else {
        ps2TraceGuestWrite(rdram, addr, 2u, value, 0u, "WRITE16", ctx);
        Ps2FastWrite16(rdram, addr, value);
    }
}
__forceinline void Write32(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint32_t value) {
    if (PS2Runtime::isSpecialAddress(addr)) runtime->Store32(rdram, ctx, addr, value);
    else {
        ps2TraceGuestWrite(rdram, addr, 4u, value, 0u, "WRITE32", ctx);
        Ps2FastWrite32(rdram, addr, value);
    }
}
__forceinline void Write64(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint64_t value) {
    if (PS2Runtime::isSpecialAddress(addr)) runtime->Store64(rdram, ctx, addr, value);
    else {
        ps2TraceGuestWrite(rdram, addr, 8u, value, 0u, "WRITE64", ctx);
        Ps2FastWrite64(rdram, addr, value);
    }
}
__forceinline void Write128(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, __m128i value) {
    if (PS2Runtime::isSpecialAddress(addr)) runtime->Store128(rdram, ctx, addr, value);
    else {
        ps2TraceGuestWrite(rdram, addr, 16u, static_cast<uint64_t>(PS2_EXTRACT_EPI64_0(value)), static_cast<uint64_t>(PS2_EXTRACT_EPI64_1(value)), "WRITE128", ctx);
        Ps2FastWrite128(rdram, addr, value);
    }
}
}
namespace cave::ps2 {
__declspec(noinline) inline void Write8(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint8_t value) {
    detail::Write8(rdram, ctx, runtime, addr, value);
}
__declspec(noinline) inline void Write16(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint16_t value) {
    detail::Write16(rdram, ctx, runtime, addr, value);
}
__declspec(noinline) inline void Write32(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint32_t value) {
    detail::Write32(rdram, ctx, runtime, addr, value);
}
__declspec(noinline) inline void Write64(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, uint64_t value) {
    detail::Write64(rdram, ctx, runtime, addr, value);
}
__declspec(noinline) inline void Write128(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t addr, __m128i value) {
    detail::Write128(rdram, ctx, runtime, addr, value);
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

// The packed operand keeps rs, rt and the signed offset in one native argument.
// These are the original instruction operations, including zero-register and
// upper-half preservation rules. PC and delay-slot state remain at the caller.
namespace cave::ps2 {
#ifdef EVAC_PS2_INLINE_RAM_ACCESSES
// Geometry-heavy titles benefit from resolving constant guest operands at the
// call site. Keep aliasing, wraparound and MMIO in the shared accessors.
template<class T, auto SlowRead>
static __forceinline T ReadGuestRam(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, uint32_t address) {
    if (address > PS2_RAM_SIZE - sizeof(T)) [[unlikely]]
        return SlowRead(rdram, ctx, runtime, address);
    T value;
    std::memcpy(&value, rdram + address, sizeof(value));
    return value;
}
template<class T, auto SlowWrite>
static __forceinline void WriteGuestRam(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime,
                                        uint32_t address, T value, const char* operation) {
    if (address > PS2_RAM_SIZE - sizeof(T)) [[unlikely]] {
        SlowWrite(rdram, ctx, runtime, address, value);
        return;
    }
    uint64_t low, high = 0;
    if constexpr (sizeof(T) == 16) {
        low = uint64_t(PS2_EXTRACT_EPI64_0(value));
        high = uint64_t(PS2_EXTRACT_EPI64_1(value));
    } else {
        low = uint64_t(value);
    }
    ps2TraceGuestWrite(rdram, address, sizeof(T), low, high, operation, ctx);
    std::memcpy(rdram + address, &value, sizeof(T));
}
#define CAVE_PS2_INSTRUCTION_ATTR static __forceinline
#define CAVE_PS2_LOAD(bits, type) ReadGuestRam<type, Read##bits>(rdram, ctx, runtime, address)
#define CAVE_PS2_STORE(bits, type, value) WriteGuestRam<type, Write##bits>(rdram, ctx, runtime, address, type(value), "WRITE" #bits)
#else
#define CAVE_PS2_INSTRUCTION_ATTR __declspec(noinline) inline
#define CAVE_PS2_LOAD(bits, type) detail::Read##bits(rdram, ctx, runtime, address)
#define CAVE_PS2_STORE(bits, type, value) detail::Write##bits(rdram, ctx, runtime, address, type(value))
#endif
#define CAVE_PS2_MEMORY_INSTRUCTION(name, operation) \
    CAVE_PS2_INSTRUCTION_ATTR void name(uint8_t* rdram, R5900Context* ctx, \
                                          PS2Runtime* runtime, uint32_t operand) { \
        const unsigned rs = (operand >> 21u) & 31u, rt = (operand >> 16u) & 31u; \
        const uint32_t offset = uint32_t(int32_t(int16_t(operand))); \
        const uint32_t address = ADD32(GPR_U32(ctx, rs), offset); \
        operation; \
    }
CAVE_PS2_MEMORY_INSTRUCTION(LoadS8, SET_GPR_S32(ctx, rt, (int8_t)CAVE_PS2_LOAD(8, uint8_t)))
CAVE_PS2_MEMORY_INSTRUCTION(LoadS16, SET_GPR_S32(ctx, rt, (int16_t)CAVE_PS2_LOAD(16, uint16_t)))
CAVE_PS2_MEMORY_INSTRUCTION(LoadS32, SET_GPR_S32(ctx, rt, (int32_t)CAVE_PS2_LOAD(32, uint32_t)))
CAVE_PS2_MEMORY_INSTRUCTION(LoadU8, SET_GPR_ZE32(ctx, rt, (uint8_t)CAVE_PS2_LOAD(8, uint8_t)))
CAVE_PS2_MEMORY_INSTRUCTION(LoadU16, SET_GPR_ZE32(ctx, rt, (uint16_t)CAVE_PS2_LOAD(16, uint16_t)))
CAVE_PS2_MEMORY_INSTRUCTION(LoadU32, SET_GPR_ZE32(ctx, rt, CAVE_PS2_LOAD(32, uint32_t)))
CAVE_PS2_MEMORY_INSTRUCTION(Load64, SET_GPR_U64(ctx, rt, CAVE_PS2_LOAD(64, uint64_t)))
CAVE_PS2_MEMORY_INSTRUCTION(Load128, SET_GPR_VEC(ctx, rt, CAVE_PS2_LOAD(128, __m128i)))
CAVE_PS2_MEMORY_INSTRUCTION(Store8, CAVE_PS2_STORE(8, uint8_t, (uint8_t)GPR_U32(ctx, rt)))
CAVE_PS2_MEMORY_INSTRUCTION(Store16, CAVE_PS2_STORE(16, uint16_t, (uint16_t)GPR_U32(ctx, rt)))
CAVE_PS2_MEMORY_INSTRUCTION(Store32, CAVE_PS2_STORE(32, uint32_t, GPR_U32(ctx, rt)))
CAVE_PS2_MEMORY_INSTRUCTION(Store64, CAVE_PS2_STORE(64, uint64_t, GPR_U64(ctx, rt)))
CAVE_PS2_MEMORY_INSTRUCTION(Store128, CAVE_PS2_STORE(128, __m128i, GPR_VEC(ctx, rt)))
#undef CAVE_PS2_MEMORY_INSTRUCTION
#undef CAVE_PS2_LOAD
#undef CAVE_PS2_STORE
#undef CAVE_PS2_INSTRUCTION_ATTR
}

#endif
