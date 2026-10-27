#include "fence_wait.h"
#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/thread/guest_memory_wait.h>
#include <atomic>

REXCVAR_DEFINE_BOOL(g2_host_fence_wait, true, "ESPGaluda2",
    "Park the native GPU completion poll; false retains the original busy loop")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
std::atomic<bool> enabled{};
std::atomic<void*> waiting_word{};
std::atomic<std::uint64_t> waits{};
void Fallback(const char* reason) {
    if (enabled.exchange(false, std::memory_order_acq_rel))
        REXLOG_WARN("G2_FENCE_WAIT fallback=original_poll reason={} waits={}",
            reason, waits.load(std::memory_order_relaxed));
}
}
namespace espgaluda2::performance {
void StartFenceWait() {
    waiting_word.store(nullptr, std::memory_order_release);
    waits.store(0, std::memory_order_relaxed);
    enabled.store(REXCVAR_GET(g2_host_fence_wait), std::memory_order_release);
    REXLOG_INFO("G2_FENCE_WAIT enabled={} native_predicate=unchanged native_policy=unchanged",
        enabled.load(std::memory_order_acquire));
}
void StopFenceWait() {
    enabled.store(false, std::memory_order_release);
    if (auto* word = waiting_word.load(std::memory_order_acquire))
        rex::thread::NotifyGuestMemoryWord(word);
    REXLOG_INFO("G2_FENCE_WAIT stopped waits={}", waits.load(std::memory_order_relaxed));
}
}

// Before the original 82173B78 poll-helper call. Do not skip that helper's
// watchdog/error handling, the surrounding comparisons, or any guest stores.
void G2WaitGpuFence(PPCContext& ctx, std::uint8_t*) {
    if (!enabled.load(std::memory_order_acquire)) return;
    auto* memory = rex::system::kernel_memory();
    using espgaluda2::performance::ReadGuestWord;
    const auto device = ctx.r31.u32;
    const auto address = ReadGuestWord(*memory, device + 10896);
    const auto submitted = ReadGuestWord(*memory, device + 10908);
    const auto completed = ReadGuestWord(*memory, address);
    if (!espgaluda2::performance::FencePending(submitted, ctx.r30.u32, completed)) return;
    const auto physical = memory->GetPhysicalAddress(address);
    if ((address & 3) || physical == UINT32_MAX) {
        Fallback("nonphysical_or_unaligned_fence");
        return;
    }
    auto* word = memory->TranslatePhysical(physical);
    waiting_word.store(word, std::memory_order_release);
    if (!enabled.load(std::memory_order_acquire)) return;
    if (waits.fetch_add(1, std::memory_order_relaxed) == 0)
        REXLOG_INFO("G2_FENCE_WAIT first address={:08X} physical={:08X} completed={} target={} submitted={}",
            address, physical, completed, ctx.r30.u32, submitted);
    if (!rex::thread::WaitGuestMemoryWord(word, __builtin_bswap32(completed)))
        Fallback("bounded_wait_failed");
}
