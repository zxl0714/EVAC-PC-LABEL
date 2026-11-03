#include "ps2_runtime_macros.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

extern const uint32_t g_ps2RecompiledFunctionTableBase = 0x100000u;
extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0x100004u;
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = 1u;
PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[1]{};

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void CheckReads(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime,
                uint32_t address, const std::array<uint8_t, 16>& expected) {
    auto scalar = [&](unsigned bytes) {
        uint64_t value{};
        std::memcpy(&value, expected.data(), bytes);
        return value;
    };
    Require(READ8(address) == scalar(1), "Byte load changed");
    Require(READ16(address) == scalar(2), "Halfword load changed");
    Require(READ32(address) == scalar(4), "Word load changed");
    Require(READ64(address) == scalar(8), "Doubleword load changed");
    const __m128i wide = READ128(address);
    Require(std::memcmp(&wide, expected.data(), expected.size()) == 0, "Quadword load changed");
    unsigned evaluations{};
    Require(READ32((++evaluations, address)) == scalar(4) && evaluations == 1,
            "Load address was evaluated more than once");
}
}

int main() {
    try {
        PS2Runtime state;
        Require(state.memory().initialize(), "Cannot allocate fixture memory");
        auto* rdram = state.memory().getRDRAM();
        auto* runtime = &state;
        R5900Context context{};
        auto* ctx = &context;
        ctx->pc = 0x123456;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x123452;
        ctx->r[7] = _mm_set_epi64x(0x1122334455667788ll, 0x7766554433221100ll);
        const auto original = context;

        for (uint32_t address : {0x100u, 0x80000100u, 0xa0000100u, 0x1fffffdu, 0x1ffffffu}) {
            std::array<uint8_t, 16> expected{};
            for (unsigned i = 0; i < expected.size(); ++i) {
                expected[i] = uint8_t(0x37 + i * 11);
                rdram[(address + i) & PS2_RAM_MASK] = expected[i];
            }
            CheckReads(rdram, ctx, runtime, address, expected);
        }
        std::array<uint8_t, 16> scratch{};
        for (unsigned i = 0; i < scratch.size(); ++i) {
            scratch[i] = uint8_t(0xb1 + i * 3);
            runtime->Store8(rdram, ctx, 0x70000010u + i, scratch[i]);
        }
        CheckReads(rdram, ctx, runtime, 0x70000010u, scratch);
        Require(ctx->pc == original.pc && ctx->in_delay_slot == original.in_delay_slot &&
                ctx->branch_pc == original.branch_pc &&
                std::memcmp(ctx->r, original.r, sizeof(original.r)) == 0,
                "Loads changed guest register or branch state");
        std::cout << "PS2 shared reads: 5 widths, RAM aliases/wrap, scratchpad, address evaluation and guest state passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
