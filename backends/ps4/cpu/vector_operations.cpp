#include "vector_fallback.h"
#include <cmath>
#include <simde/x86/sse4.2.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <cstdio>
#include <intrin.h>
#include <type_traits>

namespace cave::ps4::cpu {
namespace {
union alignas(16) Lane {
    __m128i i;
    __m128 p;
    __m128d d;
    std::uint8_t u8[16];
    std::uint16_t u16[8];
    std::uint32_t u32[4];
    std::uint64_t u64[2];
};
struct Value {
    Lane lane[2];
};
thread_local Lane upper[16]{};

std::uintptr_t Address(const VectorFrame &f, const VectorOp &op) {
    auto address = op.displacement;
    if (op.base >= 0)
        address += f.gpr[op.base];
    if (op.index >= 0)
        address += f.gpr[op.index] * op.scale;
    return op.address_width == 32 ? static_cast<std::uint32_t>(address) : address;
}
Value Read(const VectorFrame &f, const VectorOp &op, unsigned index) {
    Value v{};
    const auto [kind, reg, bytes] = op.operands[index];
    switch (kind) {
    case VectorOperand::Vector:
        std::memcpy(&v.lane[0], f.xmm[reg], 16);
        v.lane[1] = upper[reg];
        break;
    case VectorOperand::General:
        v.lane[0].u64[0] = f.gpr[reg];
        break;
    case VectorOperand::Memory:
        std::memcpy(&v, reinterpret_cast<const void *>(Address(f, op)), bytes);
        break;
    case VectorOperand::Immediate:
        v.lane[0].u8[0] = reg;
        break;
    }
    return v;
}
void Write(VectorFrame &f, const VectorOp &op, const Value &v) {
    const auto [kind, reg, bytes] = op.operands[0];
    if (kind == VectorOperand::Vector) {
        std::memcpy(f.xmm[reg], &v.lane[0], 16);
        upper[reg] = bytes == 32 ? v.lane[1] : Lane{};
    } else if (kind == VectorOperand::Memory) {
        std::memcpy(reinterpret_cast<void *>(Address(f, op)), &v, bytes);
    } else {
        f.gpr[reg] = bytes == 8 ? v.lane[0].u64[0] : v.lane[0].u32[0];
    }
}

// UCOMI and COMI differ for quiet NaNs. Issue the matching host comparison to
// preserve DAZ, exceptions and MXCSR. Compare predicates use its CF/PF/ZF.
template <bool Double> unsigned CompareFlags(const Lane &a, const Lane &b, bool signaling) {
    unsigned char below, equal, unordered;
#define COMPARE_FLAGS(name, field)                                                                             \
    asm volatile(name " %4, %3; setb %0; sete %1; setp %2"                                                     \
                 : "=qm"(below), "=qm"(equal), "=qm"(unordered)                                                \
                 : "x"(a.field), "x"(b.field)                                                                  \
                 : "cc")
    if constexpr (Double) {
        if (signaling) {
            COMPARE_FLAGS("comisd", d);
        } else {
            COMPARE_FLAGS("ucomisd", d);
        }
    } else {
        if (signaling) {
            COMPARE_FLAGS("comiss", p);
        } else {
            COMPARE_FLAGS("ucomiss", p);
        }
    }
#undef COMPARE_FLAGS
    return below | (unsigned(unordered) << 2) | (unsigned(equal) << 6);
}
bool Predicate(unsigned flags, unsigned p) {
    const bool unordered = (flags & 4) != 0, eq = (flags & 64) != 0, lt = (flags & 1) != 0;
    switch (p & 15) {
    case 0:
        return eq && !unordered;
    case 1:
        return lt && !unordered;
    case 2:
        return (lt || eq) && !unordered;
    case 3:
        return unordered;
    case 4:
        return !eq || unordered;
    case 5:
        return !lt || unordered;
    case 6:
        return !(lt || eq) || unordered;
    case 7:
        return !unordered;
    case 8:
        return eq;
    case 9:
        return lt;
    case 10:
        return lt || eq;
    case 11:
        return false;
    case 12:
        return !eq && !unordered;
    case 13:
        return !lt;
    case 14:
        return !(lt || eq);
    default:
        return true;
    }
}
template <bool Double> Lane Compare(const Lane &a, const Lane &b, unsigned pred, bool scalar) {
    Lane result = a;
    // Bit n marks the signaling variant of Intel's 32 VCMP predicates.
    constexpr std::uint32_t signaling = 0x99996666u;
    for (unsigned i = 0; i < (scalar ? 1u : Double ? 2u : 4u); ++i) {
        Lane left{}, right{};
        if constexpr (Double) {
            left.u64[0] = a.u64[i];
            right.u64[0] = b.u64[i];
        } else {
            left.u32[0] = a.u32[i];
            right.u32[0] = b.u32[i];
        }
        const auto flags = CompareFlags<Double>(left, right, (signaling >> (pred & 31)) & 1);
        const bool value = Predicate(flags, pred);
        if constexpr (Double)
            result.u64[i] = value ? UINT64_MAX : 0;
        else
            result.u32[i] = value ? UINT32_MAX : 0;
    }
    return result;
}

template <class UInt, unsigned Fraction, unsigned Exponent>
UInt RoundIntegral(UInt x, unsigned mode, bool suppress) {
    constexpr UInt sign = UInt{1} << (sizeof(UInt) * 8 - 1), fraction = (UInt{1} << Fraction) - 1;
    constexpr unsigned maxexp = (1u << Exponent) - 1, bias = maxexp / 2;
    auto mag = x & ~sign;
    const auto exp = static_cast<unsigned>(mag >> Fraction);
    if (exp == maxexp && (mag & fraction) && !(mag & (UInt{1} << (Fraction - 1)))) {
        Lane snan{};
        if constexpr (sizeof(UInt) == 8)
            snan.u64[0] = x;
        else
            snan.u32[0] = x;
        (void)CompareFlags<sizeof(UInt) == 8>(snan, snan, false);
        return x | (UInt{1} << (Fraction - 1));
    }
    if (exp >= bias + Fraction || !mag)
        return x;
    auto csr = _mm_getcsr();
    if (!exp && (csr & 0x40))
        return x & sign;
    if (mode & 4)
        mode = (csr >> 13) & 3;
    const bool negative = (x & sign) != 0;
    UInt rounded{};
    if (exp < bias) {
        const UInt half = UInt(bias - 1) << Fraction;
        const bool away = mode == 0 ? mag > half : mode == 1 ? negative : mode == 2 && !negative;
        rounded = away ? (UInt(bias) << Fraction) : 0;
    } else {
        const unsigned low = Fraction - (exp - bias);
        const UInt unit = UInt{1} << low, discarded = mag & (unit - 1);
        if (!discarded)
            return x;
        rounded = mag - discarded;
        if (mode == 0   ? discarded > unit / 2 || (discarded == unit / 2 && (rounded & unit))
            : mode == 1 ? negative
                        : mode == 2 && !negative)
            rounded += unit;
    }
    // Raise precision via a real SSE instruction when guest exceptions are unmasked.
    if (!suppress) {
        const __m128 half = _mm_set_ss(0.5f);
        int unused;
        asm volatile("cvttss2si %1,%0" : "=r"(unused) : "x"(half));
    }
    return (x & sign) | rounded;
}

bool Calculate(ZydisMnemonic m, Lane a, Lane b, Lane mask, unsigned imm, unsigned half, bool immediate,
               Lane &r) {
    switch (m) {
#define BINARY(name, field, fn)                                                                                \
    case ZYDIS_MNEMONIC_V##name:                                                                               \
        r.field = fn(a.field, b.field);                                                                        \
        break
#define UNARY(name, field, fn)                                                                                 \
    case ZYDIS_MNEMONIC_V##name:                                                                               \
        r.field = fn(a.field);                                                                                 \
        break
        BINARY(ADDPD, d, _mm_add_pd);
        BINARY(ADDPS, p, _mm_add_ps);
        BINARY(ADDSD, d, _mm_add_sd);
        BINARY(ADDSS, p, _mm_add_ss);
        BINARY(SUBPD, d, _mm_sub_pd);
        BINARY(SUBPS, p, _mm_sub_ps);
        BINARY(SUBSD, d, _mm_sub_sd);
        BINARY(SUBSS, p, _mm_sub_ss);
        BINARY(MULPD, d, _mm_mul_pd);
        BINARY(MULPS, p, _mm_mul_ps);
        BINARY(MULSD, d, _mm_mul_sd);
        BINARY(MULSS, p, _mm_mul_ss);
        BINARY(DIVPD, d, _mm_div_pd);
        BINARY(DIVPS, p, _mm_div_ps);
        BINARY(DIVSD, d, _mm_div_sd);
        BINARY(DIVSS, p, _mm_div_ss);
        BINARY(MAXPS, p, _mm_max_ps);
        BINARY(MAXSS, p, _mm_max_ss);
        BINARY(MAXSD, d, _mm_max_sd);
        BINARY(MINPS, p, _mm_min_ps);
        BINARY(MINSS, p, _mm_min_ss);
        BINARY(MINSD, d, _mm_min_sd);
    case ZYDIS_MNEMONIC_VANDPD:
    case ZYDIS_MNEMONIC_VANDPS:
        BINARY(PAND, i, _mm_and_si128);
    case ZYDIS_MNEMONIC_VANDNPD:
    case ZYDIS_MNEMONIC_VANDNPS:
        BINARY(PANDN, i, _mm_andnot_si128);
    case ZYDIS_MNEMONIC_VORPD:
    case ZYDIS_MNEMONIC_VORPS:
        BINARY(POR, i, _mm_or_si128);
    case ZYDIS_MNEMONIC_VXORPD:
    case ZYDIS_MNEMONIC_VXORPS:
        BINARY(PXOR, i, _mm_xor_si128);
        BINARY(PADDB, i, _mm_add_epi8);
        BINARY(PADDW, i, _mm_add_epi16);
        BINARY(PADDD, i, _mm_add_epi32);
        BINARY(PADDQ, i, _mm_add_epi64);
        BINARY(PSUBW, i, _mm_sub_epi16);
        BINARY(PSUBD, i, _mm_sub_epi32);
        BINARY(PSUBQ, i, _mm_sub_epi64);
        BINARY(PSUBUSW, i, _mm_subs_epu16);
        BINARY(PACKSSDW, i, _mm_packs_epi32);
        BINARY(PACKSSWB, i, _mm_packs_epi16);
        BINARY(PACKUSWB, i, _mm_packus_epi16);
        BINARY(PACKUSDW, i, simde_mm_packus_epi32);
        BINARY(PCMPEQB, i, _mm_cmpeq_epi8);
        BINARY(PCMPEQD, i, _mm_cmpeq_epi32);
        BINARY(PCMPEQQ, i, simde_mm_cmpeq_epi64);
        BINARY(PCMPGTB, i, _mm_cmpgt_epi8);
        BINARY(PCMPGTD, i, _mm_cmpgt_epi32);
        BINARY(PCMPGTQ, i, simde_mm_cmpgt_epi64);
        BINARY(PMULLD, i, simde_mm_mullo_epi32);
        BINARY(PMULUDQ, i, _mm_mul_epu32);
        BINARY(PMADDWD, i, _mm_madd_epi16);
        BINARY(PHADDD, i, simde_mm_hadd_epi32);
        BINARY(PSHUFB, i, simde_mm_shuffle_epi8);
        BINARY(PMAXSD, i, simde_mm_max_epi32);
        BINARY(PMAXUD, i, simde_mm_max_epu32);
        BINARY(PMAXUB, i, _mm_max_epu8);
        BINARY(PMINSD, i, simde_mm_min_epi32);
        BINARY(PMINUD, i, simde_mm_min_epu32);
        BINARY(PUNPCKHBW, i, _mm_unpackhi_epi8);
        BINARY(PUNPCKHWD, i, _mm_unpackhi_epi16);
        BINARY(PUNPCKHDQ, i, _mm_unpackhi_epi32);
        BINARY(PUNPCKHQDQ, i, _mm_unpackhi_epi64);
        BINARY(PUNPCKLBW, i, _mm_unpacklo_epi8);
        BINARY(PUNPCKLWD, i, _mm_unpacklo_epi16);
        BINARY(PUNPCKLDQ, i, _mm_unpacklo_epi32);
        BINARY(PUNPCKLQDQ, i, _mm_unpacklo_epi64);
        BINARY(UNPCKHPD, d, _mm_unpackhi_pd);
        BINARY(UNPCKLPD, d, _mm_unpacklo_pd);
        BINARY(UNPCKHPS, p, _mm_unpackhi_ps);
        BINARY(UNPCKLPS, p, _mm_unpacklo_ps);
        BINARY(HADDPD, d, simde_mm_hadd_pd);
        BINARY(HADDPS, p, simde_mm_hadd_ps);
    case ZYDIS_MNEMONIC_VCVTSD2SS:
        r.p = _mm_cvtsd_ss(a.p, b.d);
        break;
    case ZYDIS_MNEMONIC_VCVTSS2SD:
        r.d = _mm_cvtss_sd(a.d, b.p);
        break;
        BINARY(MOVLHPS, p, _mm_movelh_ps);
        BINARY(MOVSS, p, _mm_move_ss);
        BINARY(MOVSD, d, _mm_move_sd);
    case ZYDIS_MNEMONIC_VMOVHPS:
        r = a;
        r.u64[1] = b.u64[0];
        break;
    case ZYDIS_MNEMONIC_VMOVLPS:
    case ZYDIS_MNEMONIC_VMOVLPD:
        r = a;
        r.u64[0] = b.u64[0];
        break;
    case ZYDIS_MNEMONIC_VSQRTSS:
        r.p = _mm_move_ss(a.p, _mm_sqrt_ss(b.p));
        break;
        UNARY(RCPPS, p, _mm_rcp_ps);
        UNARY(RSQRTPS, p, _mm_rsqrt_ps);
        UNARY(SQRTPS, p, _mm_sqrt_ps);
        UNARY(PABSD, i, simde_mm_abs_epi32);
        UNARY(PMOVSXBD, i, simde_mm_cvtepi8_epi32);
        UNARY(PMOVSXBQ, i, simde_mm_cvtepi8_epi64);
        UNARY(PMOVSXWD, i, simde_mm_cvtepi16_epi32);
        UNARY(PMOVSXWQ, i, simde_mm_cvtepi16_epi64);
        UNARY(PMOVSXDQ, i, simde_mm_cvtepi32_epi64);
        UNARY(PMOVZXBD, i, simde_mm_cvtepu8_epi32);
        UNARY(PMOVZXBQ, i, simde_mm_cvtepu8_epi64);
        UNARY(PMOVZXBW, i, simde_mm_cvtepu8_epi16);
        UNARY(PMOVZXWD, i, simde_mm_cvtepu16_epi32);
        UNARY(PMOVZXDQ, i, simde_mm_cvtepu32_epi64);
    case ZYDIS_MNEMONIC_VCVTDQ2PS:
        r.p = _mm_cvtepi32_ps(a.i);
        break;
    case ZYDIS_MNEMONIC_VCVTTPS2DQ:
        r.i = _mm_cvttps_epi32(a.p);
        break;
    case ZYDIS_MNEMONIC_VMOVSLDUP:
        r.p = _mm_shuffle_ps(a.p, a.p, 0xa0);
        break;
    case ZYDIS_MNEMONIC_VMOVSHDUP:
        r.p = _mm_shuffle_ps(a.p, a.p, 0xf5);
        break;
    case ZYDIS_MNEMONIC_VMOVDDUP:
        r.d = _mm_unpacklo_pd(a.d, a.d);
        break;
    case ZYDIS_MNEMONIC_VCMPSS:
    case ZYDIS_MNEMONIC_VCMPPS:
        r = Compare<false>(a, b, imm, m == ZYDIS_MNEMONIC_VCMPSS);
        break;
    case ZYDIS_MNEMONIC_VCMPSD:
    case ZYDIS_MNEMONIC_VCMPPD:
        r = Compare<true>(a, b, imm, m == ZYDIS_MNEMONIC_VCMPSD);
        break;
    case ZYDIS_MNEMONIC_VBLENDPS:
        for (unsigned i = 0; i < 4; ++i)
            r.u32[i] = ((imm >> (4 * half + i)) & 1) ? b.u32[i] : a.u32[i];
        break;
    case ZYDIS_MNEMONIC_VBLENDPD:
        for (unsigned i = 0; i < 2; ++i)
            r.u64[i] = ((imm >> (2 * half + i)) & 1) ? b.u64[i] : a.u64[i];
        break;
    case ZYDIS_MNEMONIC_VPBLENDW:
        for (unsigned i = 0; i < 8; ++i)
            r.u16[i] = ((imm >> i) & 1) ? b.u16[i] : a.u16[i];
        break;
    case ZYDIS_MNEMONIC_VBLENDVPS:
        for (unsigned i = 0; i < 4; ++i)
            r.u32[i] = mask.u32[i] >> 31 ? b.u32[i] : a.u32[i];
        break;
    case ZYDIS_MNEMONIC_VBLENDVPD:
        for (unsigned i = 0; i < 2; ++i)
            r.u64[i] = mask.u64[i] >> 63 ? b.u64[i] : a.u64[i];
        break;
    case ZYDIS_MNEMONIC_VPBLENDVB:
        r.i = simde_mm_blendv_epi8(a.i, b.i, mask.i);
        break;
    case ZYDIS_MNEMONIC_VPERMILPS:
    case ZYDIS_MNEMONIC_VPSHUFD:
        for (unsigned i = 0; i < 4; ++i)
            r.u32[i] = a.u32[(immediate ? imm >> (2 * i) : b.u32[i]) & 3];
        break;
    case ZYDIS_MNEMONIC_VSHUFPS:
        for (unsigned i = 0; i < 4; ++i)
            r.u32[i] = (i < 2 ? a : b).u32[(imm >> (2 * i)) & 3];
        break;
    case ZYDIS_MNEMONIC_VPERMILPD:
        for (unsigned i = 0; i < 2; ++i)
            r.u64[i] = a.u64[(immediate ? imm >> (2 * half + i) : b.u64[i] >> 1) & 1];
        break;
    case ZYDIS_MNEMONIC_VSHUFPD:
        r.u64[0] = a.u64[(imm >> (2 * half)) & 1];
        r.u64[1] = b.u64[(imm >> (2 * half + 1)) & 1];
        break;
    case ZYDIS_MNEMONIC_VPSHUFLW:
    case ZYDIS_MNEMONIC_VPSHUFHW: {
        r = a;
        unsigned k = m == ZYDIS_MNEMONIC_VPSHUFHW ? 4 : 0;
        for (unsigned i = 0; i < 4; ++i)
            r.u16[k + i] = a.u16[k + ((imm >> (2 * i)) & 3)];
        break;
    }
    case ZYDIS_MNEMONIC_VPSLLDQ:
    case ZYDIS_MNEMONIC_VPSRLDQ:
        if (imm < 16) {
            if (m == ZYDIS_MNEMONIC_VPSLLDQ)
                std::memcpy(r.u8 + imm, a.u8, 16 - imm);
            else
                std::memcpy(r.u8, a.u8 + imm, 16 - imm);
        }
        break;
        BINARY(PSLLD, i, _mm_sll_epi32);
        BINARY(PSLLW, i, _mm_sll_epi16);
        BINARY(PSLLQ, i, _mm_sll_epi64);
        BINARY(PSRLD, i, _mm_srl_epi32);
        BINARY(PSRLW, i, _mm_srl_epi16);
        BINARY(PSRLQ, i, _mm_srl_epi64);
        BINARY(PSRAD, i, _mm_sra_epi32);
        BINARY(PSRAW, i, _mm_sra_epi16);
    case ZYDIS_MNEMONIC_VINSERTPS:
        r = a;
        r.u32[(imm >> 4) & 3] = b.u32[imm >> 6];
        for (unsigned i = 0; i < 4; ++i)
            if ((imm >> i) & 1)
                r.u32[i] = 0;
        break;
    case ZYDIS_MNEMONIC_VPINSRB:
        r = a;
        r.u8[imm & 15] = b.u8[0];
        break;
    case ZYDIS_MNEMONIC_VPINSRW:
        r = a;
        r.u16[imm & 7] = b.u16[0];
        break;
    case ZYDIS_MNEMONIC_VPINSRD:
        r = a;
        r.u32[imm & 3] = b.u32[0];
        break;
    case ZYDIS_MNEMONIC_VPINSRQ:
        r = a;
        r.u64[imm & 1] = b.u64[0];
        break;
    case ZYDIS_MNEMONIC_VROUNDPS:
        for (unsigned i = 0; i < 4; ++i)
            r.u32[i] = RoundIntegral<std::uint32_t, 23, 8>(a.u32[i], imm & 7, imm & 8);
        break;
    case ZYDIS_MNEMONIC_VROUNDSS:
        r = a;
        r.u32[0] = RoundIntegral<std::uint32_t, 23, 8>(b.u32[0], imm & 7, imm & 8);
        break;
    case ZYDIS_MNEMONIC_VROUNDSD:
        r = a;
        r.u64[0] = RoundIntegral<std::uint64_t, 52, 11>(b.u64[0], imm & 7, imm & 8);
        break;
    default:
        return false;
    }
#undef BINARY
#undef UNARY
    return true;
}
void MaskedMove(VectorFrame &f, const VectorOp &op) {
    const bool store = op.operands[0].kind == VectorOperand::Memory;
    const auto mask = Read(f, op, 1);
    Value data = store ? Read(f, op, 2) : Value{};
    const unsigned stride = op.mnemonic == ZYDIS_MNEMONIC_VMASKMOVPD ? 8 : 4;
    auto *memory = reinterpret_cast<std::uint8_t *>(Address(f, op));
    auto *values = reinterpret_cast<std::uint8_t *>(&data);
    const auto *masks = reinterpret_cast<const std::uint8_t *>(&mask);
    for (unsigned offset = 0; offset < op.width; offset += stride) {
        if (!(masks[offset + stride - 1] & 128))
            continue;
        if (store)
            std::memcpy(memory + offset, values + offset, stride);
        else
            std::memcpy(values + offset, memory + offset, stride);
    }
    if (!store)
        Write(f, op, data);
}
bool Execute(VectorFrame &f, const VectorOp &op) {
    const auto m = op.mnemonic;
    if (m == ZYDIS_MNEMONIC_VZEROUPPER || m == ZYDIS_MNEMONIC_VZEROALL) {
        std::memset(upper, 0, sizeof(upper));
        if (m == ZYDIS_MNEMONIC_VZEROALL)
            std::memset(f.xmm, 0, sizeof(f.xmm));
        return true;
    }
    if (m == ZYDIS_MNEMONIC_VMASKMOVPD || m == ZYDIS_MNEMONIC_VMASKMOVPS) {
        MaskedMove(f, op);
        return true;
    }
    if (op.count < 2)
        return false;
    if (m == ZYDIS_MNEMONIC_VMOVAPS || m == ZYDIS_MNEMONIC_VMOVAPD || m == ZYDIS_MNEMONIC_VMOVDQA) {
        if ((op.operands[0].kind == VectorOperand::Memory || op.operands[1].kind == VectorOperand::Memory) &&
            (Address(f, op) & (op.width - 1)))
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    }
    const bool compare =
        m == ZYDIS_MNEMONIC_VUCOMISS || m == ZYDIS_MNEMONIC_VUCOMISD || m == ZYDIS_MNEMONIC_VPTEST;
    const auto a = Read(f, op, compare ? 0 : 1);
    const auto b = compare ? Read(f, op, 1) : op.count >= 3 ? Read(f, op, 2) : Value{};
    const auto &tail = op.operands[op.count - 1];
    const bool immediate = tail.kind == VectorOperand::Immediate;
    unsigned imm = immediate ? tail.index : 0;
    Value r{};
    if (compare) {
        unsigned flags{};
        if (m == ZYDIS_MNEMONIC_VPTEST) {
            std::uint64_t common{}, outside{};
            for (unsigned i = 0; i < (op.width == 32 ? 2u : 1u); ++i)
                for (unsigned j = 0; j < 2; ++j) {
                    common |= a.lane[i].u64[j] & b.lane[i].u64[j];
                    outside |= ~a.lane[i].u64[j] & b.lane[i].u64[j];
                }
            flags = (common ? 0 : 64) | (outside ? 0 : 1);
        } else
            flags = m == ZYDIS_MNEMONIC_VUCOMISS ? CompareFlags<false>(a.lane[0], b.lane[0], false)
                                                 : CompareFlags<true>(a.lane[0], b.lane[0], false);
        f.flags = (f.flags & ~0x8d5ull) | flags;
        return true;
    }
    switch (m) {
    case ZYDIS_MNEMONIC_VMOVAPS:
    case ZYDIS_MNEMONIC_VMOVUPS:
    case ZYDIS_MNEMONIC_VMOVAPD:
    case ZYDIS_MNEMONIC_VMOVUPD:
    case ZYDIS_MNEMONIC_VMOVDQU:
    case ZYDIS_MNEMONIC_VMOVDQA:
        r = a;
        break;
    case ZYDIS_MNEMONIC_VMOVD:
        r.lane[0].u32[0] = a.lane[0].u32[0];
        break;
    case ZYDIS_MNEMONIC_VMOVQ:
        r.lane[0].u64[0] = a.lane[0].u64[0];
        break;
    case ZYDIS_MNEMONIC_VBROADCASTF128:
        r.lane[0] = r.lane[1] = a.lane[0];
        break;
    case ZYDIS_MNEMONIC_VBROADCASTSS:
        for (auto &l : r.lane)
            l.i = _mm_set1_epi32(a.lane[0].u32[0]);
        break;
    case ZYDIS_MNEMONIC_VBROADCASTSD:
        for (auto &l : r.lane)
            l.i = _mm_set1_epi64x(a.lane[0].u64[0]);
        break;
    case ZYDIS_MNEMONIC_VEXTRACTF128:
        r.lane[0] = a.lane[imm & 1];
        break;
    case ZYDIS_MNEMONIC_VINSERTF128:
        r = a;
        r.lane[imm & 1] = b.lane[0];
        break;
    case ZYDIS_MNEMONIC_VPERM2F128:
        for (unsigned i = 0; i < 2; ++i) {
            auto selector = imm >> (4 * i);
            if (!(selector & 8))
                r.lane[i] = (selector & 2 ? b : a).lane[selector & 1];
        }
        break;
    case ZYDIS_MNEMONIC_VEXTRACTPS:
    case ZYDIS_MNEMONIC_VPEXTRD:
        r.lane[0].u32[0] = a.lane[0].u32[imm & 3];
        break;
    case ZYDIS_MNEMONIC_VPEXTRB:
        r.lane[0].u32[0] = a.lane[0].u8[imm & 15];
        break;
    case ZYDIS_MNEMONIC_VPEXTRW:
        r.lane[0].u32[0] = a.lane[0].u16[imm & 7];
        break;
    case ZYDIS_MNEMONIC_VPEXTRQ:
        r.lane[0].u64[0] = a.lane[0].u64[imm & 1];
        break;
    case ZYDIS_MNEMONIC_VCVTDQ2PD:
        r.lane[0].d = _mm_cvtepi32_pd(a.lane[0].i);
        if (op.width == 32)
            r.lane[1].d = _mm_cvtepi32_pd(_mm_srli_si128(a.lane[0].i, 8));
        break;
    case ZYDIS_MNEMONIC_VCVTPD2PS:
    case ZYDIS_MNEMONIC_VCVTTPD2DQ:
        for (unsigned i = 0; i < (op.width == 32 ? 2u : 1u); ++i) {
            Lane converted{};
            if (m == ZYDIS_MNEMONIC_VCVTPD2PS)
                converted.p = _mm_cvtpd_ps(a.lane[i].d);
            else
                converted.i = _mm_cvttpd_epi32(a.lane[i].d);
            r.lane[0].u64[i] = converted.u64[0];
        }
        break;
    case ZYDIS_MNEMONIC_VCVTSI2SD:
        r.lane[0].d = op.operands[2].bytes == 8 ? _mm_cvtsi64_sd(a.lane[0].d, b.lane[0].u64[0])
                                                : _mm_cvtsi32_sd(a.lane[0].d, b.lane[0].u32[0]);
        break;
    case ZYDIS_MNEMONIC_VCVTSI2SS:
        r.lane[0].p = op.operands[2].bytes == 8 ? _mm_cvtsi64_ss(a.lane[0].p, b.lane[0].u64[0])
                                                : _mm_cvtsi32_ss(a.lane[0].p, b.lane[0].u32[0]);
        break;
    case ZYDIS_MNEMONIC_VCVTTSD2SI:
        if (op.operands[0].bytes == 8)
            r.lane[0].u64[0] = _mm_cvttsd_si64(a.lane[0].d);
        else
            r.lane[0].u32[0] = _mm_cvttsd_si32(a.lane[0].d);
        break;
    case ZYDIS_MNEMONIC_VCVTTSS2SI:
        if (op.operands[0].bytes == 8)
            r.lane[0].u64[0] = _mm_cvttss_si64(a.lane[0].p);
        else
            r.lane[0].u32[0] = _mm_cvttss_si32(a.lane[0].p);
        break;
    case ZYDIS_MNEMONIC_VMOVMSKPS:
        r.lane[0].u32[0] = _mm_movemask_ps(a.lane[0].p);
        if (op.width == 32)
            r.lane[0].u32[0] |= _mm_movemask_ps(a.lane[1].p) << 4;
        break;
    case ZYDIS_MNEMONIC_VMOVMSKPD:
        r.lane[0].u32[0] = _mm_movemask_pd(a.lane[0].d);
        if (op.width == 32)
            r.lane[0].u32[0] |= _mm_movemask_pd(a.lane[1].d) << 2;
        break;
    case ZYDIS_MNEMONIC_VPMOVMSKB:
        r.lane[0].u32[0] = _mm_movemask_epi8(a.lane[0].i);
        break;
    default: {
        if (op.count == 2 && m == ZYDIS_MNEMONIC_VMOVHPS) {
            r.lane[0].u64[0] = a.lane[0].u64[1];
            break;
        }
        if (op.count == 2 && (m == ZYDIS_MNEMONIC_VMOVSS || m == ZYDIS_MNEMONIC_VMOVSD ||
                              m == ZYDIS_MNEMONIC_VMOVLPS || m == ZYDIS_MNEMONIC_VMOVLPD)) {
            r = a;
            break;
        }
        // For a memory INSERTPS the source always comes from element zero.
        if (m == ZYDIS_MNEMONIC_VINSERTPS && op.operands[2].kind == VectorOperand::Memory)
            imm &= 63;
        const auto mask = op.count == 4 && !immediate ? Read(f, op, 3) : Value{};
        for (unsigned i = 0; i < (op.width == 32 ? 2u : 1u); ++i)
            if (!Calculate(m, a.lane[i], b.lane[i], mask.lane[i], imm, i, immediate, r.lane[i]))
                return false;
        break;
    }
    }
    Write(f, op, r);
    return true;
}
} // namespace
extern "C" void Ps4ExecuteVectorRun(VectorFrame *frame, const VectorOp *op) {
    for (;; ++op) {
        if (!Execute(*frame, *op)) {
            std::fprintf(stderr, "Unsupported guest vector instruction %s at %p\n",
                         ZydisMnemonicGetString(op->mnemonic), reinterpret_cast<void *>(op->pc));
            const ULONG_PTR location = op->pc;
            RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION, EXCEPTION_NONCONTINUABLE, 1, &location);
        }
        if (op->resume) {
            frame->resume = op->resume;
            return;
        }
    }
}
bool ExecuteUnlistedVector(CONTEXT &c, std::span<const std::uint8_t> bytes) {
    // Hand-written leaf routines can lack unwind entries. Decode only at a real
    // guest #UD, bounded by its executable segment, without changing code live.
    ZydisDecoder decoder{};
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisDecodedInstruction ins{};
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, bytes.data(), bytes.size(), &ins, operands)))
        return false;
    VectorOp op{};
    if (!DecodeVector(ins, operands, c.Rip, op))
        return false;
    VectorFrame frame{{c.Rax, c.Rcx, c.Rdx, c.Rbx, c.Rsp, c.Rbp, c.Rsi, c.Rdi, c.R8, c.R9, c.R10, c.R11, c.R12,
                       c.R13, c.R14, c.R15},
                      c.EFlags,
                      nullptr,
                      {}};
    std::memcpy(frame.xmm, c.FltSave.XmmRegisters, sizeof(frame.xmm));
    const auto previous = _mm_getcsr();
    bool success{};
    __try {
        _mm_setcsr(c.MxCsr);
        success = Execute(frame, op);
        if (success)
            c.FltSave.MxCsr = c.MxCsr = _mm_getcsr();
    } __finally {
        _mm_setcsr(previous);
    }
    if (!success)
        return false;
    // Every supported instruction has at most one general-register destination.
    if (op.count && op.operands[0].kind == VectorOperand::General) {
        DWORD64 *registers[] = {&c.Rax, &c.Rcx, &c.Rdx, &c.Rbx, &c.Rsp, &c.Rbp, &c.Rsi, &c.Rdi,
                                &c.R8,  &c.R9,  &c.R10, &c.R11, &c.R12, &c.R13, &c.R14, &c.R15};
        const auto index = op.operands[0].index;
        *registers[index] = frame.gpr[index];
    }
    c.EFlags = static_cast<DWORD>(frame.flags);
    std::memcpy(c.FltSave.XmmRegisters, frame.xmm, sizeof(frame.xmm));
    c.Rip += ins.length;
    return true;
}
} // namespace cave::ps4::cpu
