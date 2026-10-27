#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <array>
#include <atomic>
#include "native_wait.h"
#include "render_queue.h"
#include "backends/xbox360/performance/word_wait.h"
#include "backends/xbox360/performance/signal_wait.h"

REXCVAR_DEFINE_BOOL(guwange_host_frame_wait, true, "Guwange",
    "Park original frame-completion polls; false retains native busy polling")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(guwange_host_queue_wait, true, "Guwange",
    "Park an empty native renderer queue until original work is published")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(guwange_poll_callers, false, "Diagnostics",
    "Log bounded distinct native Sleep(0) call sites per thread")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
constexpr std::uint32_t kCallbackFlag=0x82C0D578, kPendingCount=0x82C0D938;
std::atomic<bool> enabled{};
cave::xbox360::performance::SignalWait queue_wait;
std::atomic<bool> queue_enabled{};
std::atomic<std::uint64_t> queue_waits{},queue_timeouts{},queue_notifications{};
struct Slot {
    cave::xbox360::performance::WordWait wait;
    std::atomic<void*> address{};
    std::atomic<std::uint64_t> waits{},notifications{},timeouts{};
    void Start() { wait.Start();address=nullptr;waits=0;notifications=0;timeouts=0; }
    void Park(std::uint8_t* base,std::uint32_t guest_address,const char* name) {
        if(!enabled.load(std::memory_order_acquire)) return;
        auto* word=reinterpret_cast<volatile std::uint32_t*>(base+guest_address);
        const auto stored=*word;
        if(!stored) return;
        auto* ptr=const_cast<std::uint32_t*>(word);
        address.store(ptr,std::memory_order_release);
        if(!enabled.load(std::memory_order_acquire)) return;
        waits.fetch_add(1,std::memory_order_relaxed);
        if(!wait.Wait(ptr,stored) && timeouts.fetch_add(1)==0)
            REXLOG_WARN("GUWANGE_FRAME_WAIT slot={} fallback=original_poll recovery=next_original_producer",name);
    }
    void Changed(std::uint8_t* base,std::uint32_t guest_address) {
        if(!enabled.load(std::memory_order_acquire)) return;
        notifications.fetch_add(1,std::memory_order_relaxed);
        wait.Changed(base+guest_address);
    }
    void Stop(const char* name) {
        if(auto* word=address.load(std::memory_order_acquire)) rex::thread::NotifyGuestMemoryWord(word);
        REXLOG_INFO("GUWANGE_FRAME_WAIT stopped slot={} waits={} notifications={} timeouts={}",
            name,waits.load(),notifications.load(),timeouts.load());
    }
} callback,pending;
std::uint32_t Load(std::uint8_t* base,std::uint32_t address) {
    return __builtin_bswap32(*reinterpret_cast<volatile std::uint32_t*>(base+address));
}
}
namespace guwange::performance {
void StartNativeWait() {
    callback.Start();pending.Start();
    queue_waits=0;queue_timeouts=0;queue_notifications=0;
    queue_enabled=REXCVAR_GET(guwange_host_queue_wait);
    queue_wait.Start(queue_enabled.load());
    REXLOG_INFO("GUWANGE_QUEUE_WAIT enabled={} native_queues=unchanged",queue_enabled.load());
    enabled.store(REXCVAR_GET(guwange_host_frame_wait),std::memory_order_release);
    REXLOG_INFO("GUWANGE_FRAME_WAIT enabled={} native_predicates=unchanged native_policy=unchanged",enabled.load());
}
void StopNativeWait() {
    enabled.store(false,std::memory_order_release);
    queue_enabled=false;queue_wait.Stop();
    callback.Stop("callback");pending.Stop("pending");
    REXLOG_INFO("GUWANGE_QUEUE_WAIT stopped waits={} notifications={} timeouts={}",
        queue_waits.load(),queue_notifications.load(),queue_timeouts.load());
}
}
// Before original Sleep(0), after flag set; all native code remains.
void GuwangeWaitCallback(PPCContext&,std::uint8_t* base) { callback.Park(base,kCallbackFlag,"callback"); }
// Immediately after the original completion store.
void GuwangeCallbackChanged(PPCContext&,std::uint8_t* base) { callback.Changed(base,kCallbackFlag); }
void GuwangeWaitPending(PPCContext& ctx,std::uint8_t* base) {
    if(!enabled.load(std::memory_order_acquire)) return;
    // Restrict the polymorphic wait to the verified count reader.
    if(Load(base,Load(base,ctx.r31.u32)+68)!=0x821F7868) return;
    pending.Park(base,kPendingCount,"pending");
}
// After original decrement AND clamp, not before completion is published.
void GuwangePendingChanged(PPCContext&,std::uint8_t* base) { pending.Changed(base,kPendingCount); }

// At the bottom of the native render loop, after all original queue locks
// have been released. A pending cleanup or flush is work too, not idle time.
void GuwangeWaitRenderQueue(PPCContext& ctx,std::uint8_t* base) {
    if(!queue_enabled.load(std::memory_order_acquire)) return;
    if(Load(base,Load(base,ctx.r25.u32)+12)!=0x8220D948) return;
    const auto idle=[&] {
        const auto cleanup=Load(base,0x82C0D974);
        if(!cleanup) return false;
        return guwange::performance::RenderQueueIdle(cleanup,Load(base,cleanup+4),
            Load(base,ctx.r28.u32+12),Load(base,ctx.r28.u32+16),
            Load(base,ctx.r30.u32+12),Load(base,ctx.r30.u32+16),
            Load(base,ctx.r23.u32),Load(base,ctx.r25.u32+44));
    };
    if(!idle()) return;
    queue_waits.fetch_add(1,std::memory_order_relaxed);
    if(!queue_wait.Wait(idle) && queue_timeouts.fetch_add(1)==0)
        REXLOG_WARN("GUWANGE_QUEUE_WAIT fallback=original_poll recovery=next_original_producer");
}

// Original resource/frame enqueues, flush request and cleanup publication.
// Each site is after its final store and, where applicable, original unlock.
void GuwangeRenderWorkChanged(PPCContext&,std::uint8_t*) {
    if(!queue_enabled.load(std::memory_order_acquire)) return;
    queue_notifications.fetch_add(1,std::memory_order_relaxed);
    queue_wait.Notify();
}

void GuwangePollCaller(PPCContext& ctx, std::uint8_t* base) {
    if (!REXCVAR_GET(guwange_poll_callers) || ctx.r3.u32 != 0) return;
    thread_local std::array<std::uint32_t, 16> seen{};
    thread_local std::size_t count{};
    for (std::size_t i = 0; i < count; ++i) if (seen[i] == ctx.lr) return;
    if (count == seen.size()) return;
    seen[count++] = static_cast<std::uint32_t>(ctx.lr);
    REXLOG_INFO("GUWANGE_POLL_CALLER return={:08X} r31={:08X} r30={:08X} r29={:08X} r28={:08X}",
        ctx.lr, ctx.r31.u32, ctx.r30.u32, ctx.r29.u32, ctx.r28.u32);
    if (ctx.lr == 0x821E96BC) {
        const auto load = [base](std::uint32_t address) {
            return __builtin_bswap32(*reinterpret_cast<volatile std::uint32_t*>(base + address));
        };
        REXLOG_INFO("GUWANGE_POLL_CALLER pending_method={:08X}", load(load(ctx.r31.u32) + 68));
    }
}
