#include "frame_delay.h"
#include "vblank_gate.h"
#include <rex/ppc.h>
#include <rex/logging.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <stdexcept>

// Existing native D3D command-buffer completion, not an approximation of it.
REX_EXTERN(sub_82174B30);

namespace {
using espgaluda2::latency::VblankGate;
VblankGate gate;
std::atomic<bool> enabled{};
// These fields belong only to the native game's presentation thread.
std::uint32_t selected_device{},saved_flags{},saved_interval{};
std::uint64_t frames{};
std::uint32_t Read(const std::uint8_t* base,std::uint32_t address) {
    std::uint32_t value;
    std::memcpy(&value,base+address,sizeof(value));
    return __builtin_bswap32(value);
}
void Write(std::uint8_t* base,std::uint32_t address,std::uint32_t value) {
    value=__builtin_bswap32(value);
    std::memcpy(base+address,&value,sizeof(value));
}
void Drain(PPCContext& ctx,std::uint8_t* base,std::uint32_t device) {
    // A side call must not use the interrupted function's linkage/red-zone.
    const auto saved=ctx;
    const auto csr=ctx.fpscr.getcsr();
    struct Restore {
        PPCContext& ctx;
        const PPCContext& saved;
        std::uint32_t csr;
        ~Restore() {ctx=saved;ctx.fpscr.setcsr(csr);}
    } restore{ctx,saved,csr};
    ctx.r1.u32-=0x200;
    ctx.r3.u32=device;
    sub_82174B30(ctx,base);
}
void RestorePolicy(std::uint8_t* base,std::uint32_t device) {
    if(selected_device!=device) return;
    const auto flags=Read(base,device+24192);
    Write(base,device+24192,(flags&~5u)|(saved_flags&5u));
    Write(base,device+13804,saved_interval);
    selected_device=0;
}
}
namespace espgaluda2::latency {
void Configure(unsigned mode) {
    if(mode!=1 && mode!=2) throw std::runtime_error("G2 Frame Delay must be 2 or 1");
    selected_device=0; frames=0;
    gate.Start();
    enabled.store(mode==1,std::memory_order_release);
    REXLOG_INFO("G2_FRAME_DELAY requested={} policy={} experimental={} restart_to_change=true",
        mode,mode==1?"cpu_vblank_immediate_swap":"original",mode==1);
}
void Stop() {enabled.store(false,std::memory_order_release);gate.Stop();}
}

void G2CompletedVblank(PPCContext& ctx,std::uint8_t*) {
    if(enabled.load(std::memory_order_acquire)) gate.Publish(ctx.r31.u32);
}

bool G2PresentCpuWait(PPCContext& ctx,std::uint8_t* base) {
    const auto device=ctx.r31.u32;
    if(!enabled.load(std::memory_order_acquire)) {
        RestorePolicy(base,device);
        return false; // Fall through to the untouched native 2V call.
    }
    const auto started=std::chrono::steady_clock::now();
    Drain(ctx,base,device);
    const auto drained=std::chrono::steady_clock::now();
    if(selected_device!=device) {
        selected_device=device;
        saved_flags=Read(base,device+24192);
        saved_interval=Read(base,device+13804);
        REXLOG_INFO("G2_FRAME_DELAY device={:08X} native_flags={:08X} native_interval={:08X}",
            device,saved_flags,saved_interval);
    }
    const auto result=gate.Wait(device,std::chrono::milliseconds(150));
    if(result!=VblankGate::Result::advanced) {
        enabled.store(false,std::memory_order_release);
        RestorePolicy(base,device);
        if(result==VblankGate::Result::stopped)
            REXLOG_INFO("G2_FRAME_DELAY action=shutdown_wait_cancelled low_frames={}",frames);
        else
            REXLOG_WARN("G2_FRAME_DELAY wait_result={} actual=2 action=retain_original_until_restart",
                static_cast<unsigned>(result));
        return false;
    }
    // Match Futari 1V's native flags. Keep all unrelated D3D behavior bits.
    Write(base,device+24192,(Read(base,device+24192)|4u)&~1u);
    Write(base,device+13804,0x80000000u);
    if(++frames%300==0) {
        const auto now=std::chrono::steady_clock::now();
        REXLOG_INFO("G2_FRAME_DELAY low_frames={} gpu_drain_us={} cpu_wait_us={}",frames,
            std::chrono::duration_cast<std::chrono::microseconds>(drained-started).count(),
            std::chrono::duration_cast<std::chrono::microseconds>(now-drained).count());
    }
    // Skip only the queued one-VBlank synchronization call. Native resolve,
    // state restoration, swap, buffer retirement and rendering remain intact.
    return true;
}
