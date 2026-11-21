#include "runtime/ps2_vu1_jit.h"

#include <array>
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
    const unsigned saved = _mm_getcsr();
    constexpr Bits mantissas{0, 1, 0x3fffff, 0x7fffff};
    for (unsigned mode : {0u, 0x40u, 0x8000u, 0x8040u}) {
        _mm_setcsr((saved & ~(0x8040u | _MM_ROUND_MASK)) | mode | _MM_ROUND_TOWARD_ZERO);
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
