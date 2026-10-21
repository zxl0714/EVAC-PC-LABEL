// Compiles the real generated callback, not a handwritten replacement.
// The original callee is intercepted; this is not a whole-mode gameplay test.
#include <rex/ppc/context.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

unsigned checks{},calls{};
PPCContext observed{};
std::uint8_t* observed_base{};
void Check(bool condition) {
    ++checks;
    if(!condition)throw std::runtime_error("Novice callback ABI mismatch");
}
void sub_880EE9E8(PPCContext& ctx,std::uint8_t* base) {
    ++calls;observed=ctx;observed_base=base;
    ctx.r3.u64=0xabcdef0100000000ull+calls;
}
#undef REX_FUNC_PROLOGUE
#define REX_FUNC_PROLOGUE() ((void)0)
#undef DEFINE_REX_FUNC
#define DEFINE_REX_FUNC(name) void name(PPCContext& ctx,std::uint8_t* base)
#include "callback.inc"

int main() {
    try {
        std::array<std::uint8_t,16> memory{};
        for(const auto argument: {0ull,1ull,0x88776655ull,0xffff123488776655ull}) {
            PPCContext ctx{};
            ctx.r1.u64=0x1234567800123450ull;
            ctx.r3.u64=0x1234567888001000ull;
            ctx.r4.u64=0x99999999ull;ctx.r5.u64=argument;
            ctx.r31.u64=0xfedcba9888880000ull;ctx.lr=0x88024aec;
            const auto before=ctx;
            using Callback=void (*)(PPCContext&,std::uint8_t*);
            const Callback callback=sub_880EEB00;
            const auto previous=calls;
            callback(ctx,memory.data());
            Check(calls==previous+1 && observed_base==memory.data());
            auto expected=before;expected.r4=before.r5;expected.r5.s64=0;
            Check(std::memcmp(&expected,&observed,sizeof(ctx))==0);
            expected.r3.u64=0xabcdef0100000000ull+calls;
            Check(std::memcmp(&expected,&ctx,sizeof(ctx))==0);
        }
        std::cout << "Novice generated callback: " << checks << " checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';return 1;
    }
}
