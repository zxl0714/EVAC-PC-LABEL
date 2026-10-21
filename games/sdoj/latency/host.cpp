#include "frame_delay.h"
#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <windows.h>
#include <atomic>
#include <stdexcept>

namespace {
std::atomic<unsigned> requested{3};
std::atomic<bool> running{};
std::atomic<unsigned> timeouts{};
}
REXCVAR_DEFINE_BOOL(sdoj_frame_delay_trace, false, "Diagnostics", "Trace bounded SDOJ Frame Delay requests and input-prefix receipts")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
namespace sdoj::latency {
void Configure(unsigned mode) {
    if (mode < 1 || mode > 3) throw std::runtime_error("SDOJ Frame Delay must be 3, 2 or 1");
    timeouts.store(0, std::memory_order_relaxed);
    requested.store(mode, std::memory_order_relaxed);
    running.store(true, std::memory_order_release);
    REXLOG_INFO("SDOJ_FRAME_DELAY requested={} effective_policy={} policy={} restart_to_change=true",
        mode, mode, mode == 3 ? "original" : mode == 2 ? "balanced" : "low");
}
void Stop() {
    running.store(false, std::memory_order_release);
    // Modules may still have an interrupted native swap to settle. Stop new
    // work without unloading a PE or erasing its successful late-call receipt.
    using StopModule = void (*)();
    for (const auto* name : {L"sdoj_CA022100.dll", L"sdoj_CA022110.dll"})
        if (const auto module = GetModuleHandleW(name))
            if (const auto stop = reinterpret_cast<StopModule>(GetProcAddress(module, "SdojFrameDelayStop"))) stop();
}
}
extern "C" __declspec(dllexport) unsigned SdojFrameDelayPolicy() {
    if (!running.load(std::memory_order_acquire)) return 3;
    const auto mode = requested.load(std::memory_order_relaxed);
    return mode == 1 && timeouts.load(std::memory_order_acquire) >= 3 ? 2 : mode;
}
extern "C" __declspec(dllexport) unsigned SdojFrameDelayRequested() { return requested.load(); }
extern "C" __declspec(dllexport) bool SdojFrameDelayTrace() { return REXCVAR_GET(sdoj_frame_delay_trace); }
extern "C" __declspec(dllexport) void SdojFrameDelayTimeout() {
    const auto total = timeouts.fetch_add(1, std::memory_order_acq_rel) + 1;
    REXLOG_WARN("SDOJ_FRAME_DELAY completion_timeout={} actual={} action={} config_unchanged=true",
        total, total >= 3 ? "balanced" : "draining", total >= 3 ? "drain_and_latch_balanced_until_process_exit" : "drain_before_next_request");
}
void SdojDelayAdvance(PPCContext& ctx, std::uint8_t* base) {
    // Keep forwarding after a process-wide downgrade until an already-started
    // module request has drained. Otherwise its writer would remain suppressed.
    if (!running.load(std::memory_order_acquire) || requested.load(std::memory_order_relaxed)!=1) return;
    std::uint32_t selection;
    std::memcpy(&selection, base + ctx.r31.u32 + 152, sizeof(selection));
    const auto module = GetModuleHandleW(__builtin_bswap32(selection) == 2 ? L"sdoj_CA022110.dll" : L"sdoj_CA022100.dll");
    using Advance = void (*)(PPCContext&, std::uint8_t*);
    if (module) if (const auto advance = reinterpret_cast<Advance>(GetProcAddress(module, "SdojFrameDelayAdvance"))) advance(ctx, base);
}
