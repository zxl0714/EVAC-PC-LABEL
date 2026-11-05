#pragma once
#include <rex/ppc.h>
#include <cstdint>

namespace cave::xbox360::performance {
// The title has already armed this callback before issuing its swap. Wait for
// that same store, including its original guest yield/APC path. The return
// registers and linkage of the interrupted swap must survive the side call.
template<class Park, class Yield, class Running>
void FinishPostSwapWait(PPCContext& ctx, std::uint8_t* base, std::uint32_t flag,
                        Park park, Yield yield, Running running) {
    if (!running()) return;
    PPCContext scratch = ctx;
    const auto csr = ctx.fpscr.getcsr();
    struct Restore {
        PPCContext& ctx;
        std::uint32_t csr;
        ~Restore() { ctx.fpscr.setcsr(csr); }
    } restore{ctx, csr};
    scratch.r1.u32 -= 0x200;
    const auto* pending = reinterpret_cast<volatile const std::uint32_t*>(base + flag);
    do {
        park();
        scratch.r3.u64 = 0;
        yield(scratch, base);
    } while (*pending && running());
}
}
