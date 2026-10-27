#include "native_wait.h"
#include "word_wait.h"
#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/thread/guest_memory_wait.h>
#include <atomic>
#include <array>

REXCVAR_DEFINE_BOOL(sdoj_host_callback_wait, true, "SDOJ",
    "Park the original frame callback flag poll; false retains busy polling")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(sdoj_poll_callers, false, "Diagnostics",
    "Log bounded distinct native Sleep(0) call sites per thread")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(sdoj_host_tick_wait, true, "SDOJ",
    "Park the original module tick poll without changing frame delay")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
extern "C" __declspec(dllexport) bool SdojNativeTickWaitEnabled() {
    return REXCVAR_GET(sdoj_host_tick_wait);
}

namespace {
constexpr std::uint32_t kCallbackFlag = 0x8279C770;
std::atomic<bool> enabled{};
std::atomic<void*> waiting_word{};
std::atomic<void*> pending_word{};
std::atomic<std::uint64_t> waits{}, notifications{};
sdoj::performance::WordWait callback_wait, pending_wait;
std::atomic<unsigned> callback_timeouts{}, pending_timeouts{};
}
namespace sdoj::performance {
void StartNativeWait() {
    waiting_word.store(nullptr, std::memory_order_release);
    pending_word.store(nullptr, std::memory_order_release);
    waits = 0; notifications = 0;
    callback_timeouts = 0; pending_timeouts = 0;
    callback_wait.Start(); pending_wait.Start();
    enabled.store(REXCVAR_GET(sdoj_host_callback_wait), std::memory_order_release);
    REXLOG_INFO("SDOJ_CALLBACK_WAIT enabled={} original_flag=8279C770 original_policy=unchanged",
        enabled.load());
}
void StopNativeWait() {
    enabled.store(false, std::memory_order_release);
    if (auto* word = waiting_word.load(std::memory_order_acquire))
        rex::thread::NotifyGuestMemoryWord(word);
    if (auto* word = pending_word.load(std::memory_order_acquire))
        rex::thread::NotifyGuestMemoryWord(word);
    REXLOG_INFO("SDOJ_CALLBACK_WAIT stopped waits={} notifications={} callback_timeouts={} pending_timeouts={}",
        waits.load(), notifications.load(), callback_timeouts.load(), pending_timeouts.load());
}
}
// The hooks precede the two original Sleep(0) loops. Neither the callback flag
// stores nor the native drain, time-budget and frame-delay logic are replaced.
void SdojWaitCallback(PPCContext&, std::uint8_t* base) {
    if (!enabled.load(std::memory_order_acquire)) return;
    auto* word = reinterpret_cast<volatile std::uint32_t*>(base + kCallbackFlag);
    const auto stored = *word;
    if (!stored) return;
    auto* address = const_cast<std::uint32_t*>(word);
    waiting_word.store(address, std::memory_order_release);
    if (!enabled.load(std::memory_order_acquire)) return;
    waits.fetch_add(1, std::memory_order_relaxed);
    if (!callback_wait.Wait(address, stored) && callback_timeouts.fetch_add(1) == 0)
        REXLOG_WARN("SDOJ_CALLBACK_WAIT fallback=original_poll reason=bounded_wait_failed recovery=next_original_producer");
}
// Immediately AFTER the original callback's store of zero.
void SdojCallbackChanged(PPCContext&, std::uint8_t* base) {
    if (!enabled.load(std::memory_order_acquire)) return;
    notifications.fetch_add(1, std::memory_order_relaxed);
    callback_wait.Changed(base + kCallbackFlag);
}
// The render-thread caller waits on vtable+68. Only the audited implementation
// (8219DE60: read 8279C774) is eligible; other polymorphic objects stay native.
void SdojWaitPendingFrames(PPCContext& ctx, std::uint8_t* base) {
    if (!enabled.load(std::memory_order_acquire)) return;
    const auto load = [base](std::uint32_t address) {
        return __builtin_bswap32(*reinterpret_cast<volatile std::uint32_t*>(base + address));
    };
    if (load(load(ctx.r27.u32) + 68) != 0x8219DE60) return;
    auto* word = reinterpret_cast<volatile std::uint32_t*>(base + kCallbackFlag + 4);
    const auto stored = *word;
    if (!stored) return;
    auto* address = const_cast<std::uint32_t*>(word);
    pending_word.store(address, std::memory_order_release);
    if (!enabled.load(std::memory_order_acquire)) return;
    waits.fetch_add(1, std::memory_order_relaxed);
    if (!pending_wait.Wait(address, stored) && pending_timeouts.fetch_add(1) == 0)
        REXLOG_WARN("SDOJ_CALLBACK_WAIT fallback=original_poll reason=pending_wait_failed recovery=next_original_producer");
}
// After decrement AND the original nonnegative clamp, before releasing its lock.
void SdojPendingFramesChanged(PPCContext&, std::uint8_t* base) {
    if (!enabled.load(std::memory_order_acquire)) return;
    notifications.fetch_add(1, std::memory_order_relaxed);
    pending_wait.Changed(base + kCallbackFlag + 4);
}
void SdojPollCaller(PPCContext& ctx, std::uint8_t*) {
    if (!REXCVAR_GET(sdoj_poll_callers) || ctx.r3.u32 != 0) return;
    thread_local std::array<std::uint32_t, 16> seen{};
    thread_local std::size_t count{};
    for (std::size_t i = 0; i < count; ++i) if (seen[i] == ctx.lr) return;
    if (count == seen.size()) return;
    seen[count++] = static_cast<std::uint32_t>(ctx.lr);
    REXLOG_INFO("SDOJ_POLL_CALLER return={:08X} r31={:08X} r30={:08X} r29={:08X} r28={:08X}",
        ctx.lr, ctx.r31.u32, ctx.r30.u32, ctx.r29.u32, ctx.r28.u32);
}
