#pragma once
#include <rex/ppc/context.h>
#include <cstdint>

namespace sdoj::latency {
// The interrupted caller's linkage/red-zone is not side-call scratch space.
// Preserve the complete emulated context AND the host floating-point control
// state: native save/restore helpers can change the host flush/rounding modes.
class GuestCall {
public:
    explicit GuestCall(PPCContext& ctx) : ctx_(ctx),saved_(ctx),csr_(ctx.fpscr.getcsr()) {
        ctx.r1.u32-=0x200;
    }
    ~GuestCall() { ctx_=saved_; ctx_.fpscr.setcsr(csr_); }
    GuestCall(const GuestCall&)=delete;
    GuestCall& operator=(const GuestCall&)=delete;
private:
    PPCContext& ctx_;
    PPCContext saved_;
    std::uint32_t csr_;
};
}
