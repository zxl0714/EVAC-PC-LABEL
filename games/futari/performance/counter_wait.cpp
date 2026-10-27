#include "counter_wait.h"
#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>

REXCVAR_DEFINE_BOOL(futari_host_vblank_wait, true, "Futari",
    "Park the native VBlank counter poll; false retains the original busy loop")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
constexpr std::uint32_t kCounter = 0x821F8560;
std::atomic<bool> enabled{};
std::atomic<void*> waiting_word{};
std::atomic<std::uint64_t> waits{}, notifications{};
void Wait(std::uint8_t* base, std::uint64_t expected) {
    if (!enabled.load(std::memory_order_acquire)) return;
    void* word = base + kCounter;
    waiting_word.store(word, std::memory_order_release);
    if (!enabled.load(std::memory_order_acquire)) return;
    waits.fetch_add(1, std::memory_order_relaxed);
    if (!futari::performance::WaitCounter(word, __builtin_bswap64(expected))) {
        const auto error = GetLastError();
        if (enabled.exchange(false, std::memory_order_acq_rel))
            REXLOG_WARN("FUTARI_COUNTER_WAIT fallback=original_poll error={} waits={}",
                error, waits.load(std::memory_order_relaxed));
    }
}
}
namespace futari::performance {
void StartCounterWait() {
    waiting_word.store(nullptr, std::memory_order_release);
    waits = 0; notifications = 0;
    enabled.store(REXCVAR_GET(futari_host_vblank_wait), std::memory_order_release);
    REXLOG_INFO("FUTARI_COUNTER_WAIT enabled={} native_counter=821F8560 native_compare=unchanged",
        enabled.load(std::memory_order_acquire));
}
void StopCounterWait() {
    enabled.store(false, std::memory_order_release);
    if (auto* word = waiting_word.load(std::memory_order_acquire)) NotifyCounter(word);
    REXLOG_INFO("FUTARI_COUNTER_WAIT stopped waits={} notifications={}",
        waits.load(std::memory_order_relaxed), notifications.load(std::memory_order_relaxed));
}
}
// Hooks run before their named instruction. Both original loop bodies remain.
void FutariWaitVblank1(PPCContext& ctx, std::uint8_t* base) { Wait(base, ctx.r10.u64); }
void FutariWaitVblank0(PPCContext& ctx, std::uint8_t* base) { Wait(base, ctx.r11.u64); }
void FutariVblankCounterChanged(PPCContext&, std::uint8_t* base) {
    if (!enabled.load(std::memory_order_acquire)) return;
    notifications.fetch_add(1, std::memory_order_relaxed);
    futari::performance::NotifyCounter(base + kCounter);
}
