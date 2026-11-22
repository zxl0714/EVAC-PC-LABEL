#include "runtime/ps2_vu1_jit.h"

#include "runtime/ps2_vu_clip.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>

namespace {
using Bits = std::array<uint32_t, 4>;

__m128 load(const Bits& bits) {
    return _mm_castsi128_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(bits.data())));
}

// Independently truncate the smaller input's mantissa before the rounded sum.
__m128 reference(Bits a, Bits b) {
    for (unsigned lane = 0; lane < 4; ++lane) {
        unsigned ea = (a[lane] >> 23) & 255, eb = (b[lane] >> 23) & 255;
        if (ea == eb || !ea || !eb) continue;
        unsigned distance = ea > eb ? ea - eb : eb - ea;
        auto& smaller = ea > eb ? b[lane] : a[lane];
        smaller &= distance >= 24 ? 0x80000000u : (~0u << distance);
    }
    return _mm_add_ps(load(a), load(b));
}
bool maxZeroMatches() {
    VU1Interpreter vu;
    constexpr Bits fractions{0u, 1u, 0x003fffffu, 0x007fffffu};
    const auto sentinel = _mm_set1_epi32(0x12345678);
    for (unsigned exp = 0; exp < 256; ++exp) {
        for (uint32_t sign : {0u, 0x80000000u}) {
            Bits inputs{}, expected{};
            for (unsigned i = 0; i < inputs.size(); ++i) {
                inputs[i] = sign | (exp << 23) | fractions[i];
                expected[i] = std::bit_cast<uint32_t>(ps2_vu1_detail::vuMax(std::bit_cast<float>(inputs[i]), 0.0f));
            }
            auto result = _mm_castsi128_ps(sentinel);
            Vu1Jit::maxMini<0x01e0ef50u, true>(vu, result, load(inputs), Vu1Jit::vf0());
            if (_mm_movemask_epi8(_mm_cmpeq_epi32(_mm_castps_si128(result), _mm_castps_si128(load(expected)))) != 0xffff)
                return false;
            // MAXz to W only leaves the other destination lanes untouched.
            result = _mm_castsi128_ps(sentinel);
            Vu1Jit::maxMini<0x0020ef52u, true>(vu, result, load(inputs), Vu1Jit::vf0());
            expected[0] = expected[1] = expected[2] = 0x12345678;
            if (_mm_movemask_epi8(_mm_cmpeq_epi32(_mm_castps_si128(result), _mm_castps_si128(load(expected)))) != 0xffff)
                return false;
        }
    }
    return true;
}

bool clipFlagsMatch() {
    constexpr Bits fractions{0u, 1u, 0x003fffffu, 0x007fffffu};
    for (unsigned exp = 0; exp < 256; ++exp) {
        for (auto frac : fractions) {
            const uint32_t w = (exp << 23) | frac;
            const uint32_t below = w ? w - 1 : 0;
            const uint32_t above = w < 0x7fffffffu ? w + 1 : w;
            for (uint32_t signs = 0; signs < 16; ++signs) {
                Bits xyz{above, w, below, ~w};
                for (unsigned i = 0; i < 3; ++i)
                    xyz[i] |= ((signs >> i) & 1u) << 31;
                Bits limit{0u, 0u, 0u, w | ((signs & 8u) << 28)};
                alignas(16) float lanes[4];
                for (unsigned turn = 0; turn < 3; ++turn) {
                    _mm_store_ps(lanes, load(xyz));
                    if (Vu1Jit::clipFlags(load(xyz), load(limit)) !=
                        ps2VuClipJudge(lanes, std::bit_cast<float>(limit[3])))
                        return false;
                    std::rotate(xyz.begin(), xyz.begin() + 1, xyz.begin() + 3);
                }
            }
        }
    }
    return true;
}
template<uint32_t op>
bool liveFlagsMatch(__m128 left, __m128 right, __m128 acc, const Bits& expected,
                    uint32_t mac, uint32_t flags, uint32_t sticky) {
    VU1Interpreter vu;
    __m128 result = _mm_setzero_ps();
    Vu1Jit::fmacLive<0x01e00000u | op, 0u, false>(vu, result, left, right, acc);
    int32_t ignored = 0;
    Vu1Jit::flagRead<0x2400003fu>(vu, 4, ignored, 0);
    Bits bits;
    _mm_storeu_si128(reinterpret_cast<__m128i*>(bits.data()), _mm_castps_si128(result));
    return bits == expected && vu.state().mac == mac &&
           (vu.state().status & 0x3cfu) == (flags | (sticky << 6));
}

bool fmacBoundariesMatch() {
    const auto zero = _mm_setzero_ps(), one = _mm_set1_ps(1.0f);
    // An exact zero in one lane must leave the other lanes' signs visible.
    const auto mixed = _mm_setr_ps(0.0f, -1.0f, 2.0f, 3.0f);
    if (!liveFlagsMatch<0x29>(mixed, one, zero, {0u, 0xbf800000u, 0x40000000u, 0x40400000u},
                              0x48, 3, 3)) return false;
    const auto left = _mm_setr_ps(0x1p-100f, -0x1p100f, 2.0f, -3.0f);
    const auto right = _mm_setr_ps(0x1p-100f, 0x1p100f, 2.0f, -2.0f);
    if (!liveFlagsMatch<0x2a>(left, right, zero, {0u, 0xff7fffffu, 0x40800000u, 0x40c00000u},
                              0x4848, 15, 15)) return false;
    // A tiny intermediate product contributes sticky flags even if the sum is normal.
    return liveFlagsMatch<0x29>(_mm_set1_ps(0x1p-100f), _mm_set1_ps(0x1p-100f), one,
                                {0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u}, 0, 0, 5);
}

template <uint32_t op, uint32_t mask>
bool vf0Matches(VU1Interpreter& vu, __m128 left, __m128 acc, __m128 seed) {
    constexpr uint32_t upper = (mask << 21) | op;
    constexpr auto hints = VU1Interpreter::kHintFlagsUnobserved;
    auto reference = seed;
    auto actual = seed;
    Vu1Jit::fmac<upper, hints>(vu, reference, left, Vu1Jit::vf0(), acc);
    Vu1Jit::fmacWithVf0<upper, hints>(vu, actual, left, Vu1Jit::vf0(), acc);
    return _mm_movemask_epi8(_mm_cmpeq_epi32(_mm_castps_si128(reference), _mm_castps_si128(actual))) == 0xffff;
}

bool constantBroadcastsMatch() {
    VU1Interpreter vu;
    const auto seed = _mm_setr_ps(7.0f, 3.0f, 5.0f, 1.0f);
    for (uint32_t exp = 0; exp < 256; ++exp) {
        const uint32_t high = exp << 23;
        Bits bits{high, high | 0x80000000u, high | 0x7fffffu, high | 0x807fffffu};
        const auto left = load(bits);
        std::rotate(bits.begin(), bits.begin() + 1, bits.end());
        const auto acc = load(bits);
        if (!vf0Matches<0x00, 15>(vu, left, acc, seed) || !vf0Matches<0x05, 7>(vu, left, acc, seed) ||
            !vf0Matches<0x0a, 14>(vu, left, acc, seed) || !vf0Matches<0x0c, 6>(vu, left, acc, seed) ||
            !vf0Matches<0x18, 15>(vu, left, acc, seed) || !vf0Matches<0x1b, 15>(vu, left, acc, seed) ||
            !vf0Matches<0x03, 15>(vu, left, acc, seed)) return false;
    }
    return true;
}

template<unsigned fraction>
bool conversionsMatch(Bits bits) {
    constexpr unsigned shift[] = {0, 4, 12, 15};
    constexpr float scale = float(1u << shift[fraction]);
    const auto input = load(bits);
    __m128 asInt = _mm_setzero_ps(), asFloat = _mm_setzero_ps();
    Vu1Jit::ftoi<0x01e0017cu + fraction, 0>(asInt, input);
    Vu1Jit::itof<0x01e0013cu + fraction>(asFloat, input);
    Bits ints, floats;
    _mm_storeu_si128(reinterpret_cast<__m128i*>(ints.data()), _mm_castps_si128(asInt));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(floats.data()), _mm_castps_si128(asFloat));
    for (unsigned i = 0; i < 4; ++i) {
        const auto exponent = (bits[i] >> 23) & 255u;
        const auto sign = bits[i] & 0x80000000u;
        const auto normalized = exponent == 0 ? sign : exponent == 255 ? sign | 0x7f7fffffu : bits[i];
        const auto expectedInt = ps2_vu1_detail::vuFloatToInt(std::bit_cast<float>(normalized), scale);
        const auto expectedFloat = float(std::bit_cast<int32_t>(bits[i])) / scale;
        if (ints[i] != uint32_t(expectedInt) || floats[i] != std::bit_cast<uint32_t>(expectedFloat)) return false;
    }
    return true;
}

}

int main() {
    if (!maxZeroMatches()) {
        std::fprintf(stderr, "VU MAX against VF0 differs from scalar comparisons\n");
        return 1;
    }
    if (!clipFlagsMatch()) {
        std::fprintf(stderr, "VU clip flags differ from scalar comparisons\n");
        return 1;
    }
    const unsigned saved = _mm_getcsr();
    constexpr Bits mantissas{0, 1, 0x3fffff, 0x7fffff};
    for (unsigned mode : {0u, 0x40u, 0x8000u, 0x8040u}) {
        _mm_setcsr((saved & ~(0x8040u | _MM_ROUND_MASK)) | mode | _MM_ROUND_TOWARD_ZERO);
        if (!constantBroadcastsMatch()) {
            _mm_setcsr(saved);
            std::fprintf(stderr, "VU constant broadcast differs from ordinary FMAC (MXCSR %x)\n", mode);
            return 1;
        }
        if (!fmacBoundariesMatch()) {
            _mm_setcsr(saved);
            std::fprintf(stderr, "VU multiply/add flags differ at a numerical boundary (MXCSR %x)\n", mode);
            return 1;
        }
        for (unsigned exponent = 0; exponent < 256; ++exponent) {
            const unsigned base = exponent << 23;
            const Bits source{base, base | 0x7fffffu, base | 0x80000001u, base | 0x807fffffu};
            if (!conversionsMatch<0>(source) || !conversionsMatch<1>(source) ||
                !conversionsMatch<2>(source) || !conversionsMatch<3>(source)) {
                _mm_setcsr(saved);
                std::fprintf(stderr, "VU conversion mismatch: exponent %u, MXCSR mode %x\n", exponent, mode);
                return 1;
            }
        }
        for (unsigned ea = 0; ea < 256; ++ea) {
            for (unsigned eb = 0; eb < 256; ++eb) {
                for (unsigned pattern = 0; pattern < 16; ++pattern) {
                    Bits a, b;
                    for (unsigned i = 0; i < 4; ++i) {
                        a[i] = ((pattern & 1u) << 31) | (ea << 23) | mantissas[i];
                        b[i] = ((pattern & 2u) << 30) | (eb << 23) | mantissas[(i + (pattern >> 2)) & 3];
                    }
                    const __m128 actual = ps2_vu1_detail::vuAdd4(load(a), load(b));
                    // With two NaNs, the compiler can choose either SSE operand
                    // as the destination. Match the original vector path's payload.
                    const __m128 expected = ea == 255 && eb == 255
                        ? ps2_vu1_detail::vuAdd4ByBits(load(a), load(b)) : reference(a, b);
                    auto equal = _mm_cmpeq_epi32(_mm_castps_si128(actual), _mm_castps_si128(expected));
                    if (ea < 255 && eb < 255) {
                        const auto finite = ps2_vu1_detail::vuAdd4<true>(load(a), load(b));
                        equal = _mm_and_si128(equal, _mm_cmpeq_epi32(_mm_castps_si128(finite), _mm_castps_si128(expected)));
                    }
                    if (_mm_movemask_epi8(equal) != 0xffff) {
                        _mm_setcsr(saved);
                        std::fprintf(stderr, "VU add mismatch: exponents %u/%u, pattern %u, MXCSR mode %x\n",
                                     ea, eb, pattern, mode);
                        return 1;
                    }
                }
            }
        }
    }
    _mm_setcsr(saved);
}
