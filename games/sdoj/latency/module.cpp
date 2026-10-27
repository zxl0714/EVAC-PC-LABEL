#include "handoff.h"
#include "low_handoff.h"
#include "native_prefix.h"
#include "guest_call.h"
#include "render_metrics.h"
#include "../performance/tick_wait.h"
#include <rex/ppc.h>
#include <rex/memory.h>
#include <rex/logging.h>
#include <rex/input/guest_input_frame.h>
#include <windows.h>
#include <array>
#include <cstring>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <stdexcept>

// Compiled separately into the two overlapping guest modules, never the host.
#if SDOJ_MODULE_ID == 1
void SdojCoreNormalCopy(PPCRegister&);
void SdojLatencyNormalCommandsBegin(PPCRegister&);
void SdojRenderNormalWaitEnd();
void SdojLatencyNormalSource(PPCRegister&);
void SdojLatencyNormalRenderEnd();
#else
void SdojCoreArrangeCopy(PPCRegister&);
void SdojLatencyArrangeCommandsBegin(PPCRegister&);
void SdojRenderArrangeWaitEnd();
void SdojLatencyArrangeSource(PPCRegister&);
void SdojLatencyArrangeRenderEnd();
#endif
namespace {
using namespace sdoj::latency;
constexpr unsigned Module = SDOJ_MODULE_ID;
constexpr std::uint32_t Offset = Module == 1 ? 0 : 0x8528;
constexpr std::uint32_t Callback = Module == 1 ? 0x88051350 : 0x880512C8;
constexpr std::uint32_t WaitReturn = Module == 1 ? 0x88052180 : 0x88052134;
constexpr std::uint32_t ResourceWaitReturn = Module == 1 ? 0x8806F384 : 0x8806FDBC;
constexpr std::uint32_t SwapReturn = Module == 1 ? 0x88051398 : 0x88051310;
std::mutex gate;
sdoj::performance::TickWait native_tick_wait;
std::atomic<unsigned> native_tick_timeouts{};
Handoff handoff;
LowHandoff low;
DrawRecovery draw_recovery;
RenderMetrics render_metrics;
struct WrapperMetrics {
    std::uint64_t logic;
    std::uint32_t renderer;
    RenderMetrics::Pair parser;
    struct Deferred { std::uint32_t frame,calls; };
    std::optional<Deferred> deferred;
};
std::optional<WrapperMetrics> wrapper_metrics;
std::condition_variable completion_changed;
unsigned policy = 3;
unsigned requested = 3;
unsigned effective = 3;
std::array<std::uint64_t, 13> skipped{};
std::uint64_t publications{}, receipts{}, parks{};
std::uint64_t requests{}, timeouts{}, slow_frames{}, prefix_receipts{}, driver_receipts{}, wait_max_us{};
std::uint64_t low_boundaries{}, low_completed{}, low_publications{}, low_draining{}, low_budget_skips{},
              low_state_skips{}, low_control_skips{}, low_policy_skips{}, low_ownership_skips{};
std::uint64_t low_intermediate_yields{};
std::uint64_t metrics_selected{},metrics_changed{},metrics_held{},low_metrics_skips{};
std::uint64_t low_unparsed_skips{},low_resyncs{},metrics_finalized{},metrics_native_changes{};
DWORD logic_thread{};
thread_local bool input_only{}, early_driver{};
NativePrefixResult prefix_result;
std::uint64_t Now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
template<class Function> Function HostFunction(const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(GetModuleHandleW(nullptr), name));
}
bool Trace() {
    static const auto enabled = HostFunction<bool (*)()>("SdojFrameDelayTrace");
    return enabled && enabled();
}
struct FlagScope {
    bool& flag; bool previous;
    explicit FlagScope(bool& target) : flag(target), previous(target) { flag=true; }
    ~FlagScope() { flag=previous; }
};

std::uint32_t Word(const std::uint8_t* base, std::uint32_t address) {
    std::uint32_t word;
    std::memcpy(&word, base + address, 4);
    return __builtin_bswap32(word);
}
NativeState Snapshot(const std::uint8_t* base) {
    NativeState state;
    state.frame = Word(base, 0x82633548);
    state.renderer = Word(base, 0x888791D8 + Offset);
    state.callback = Word(base, 0x886B41F4 + Offset);
    state.serial = Word(base, 0x886F4CD0 + Offset);
    state.calls = Word(base, 0x886B41EC + Offset);
    state.countdown = Word(base, 0x88879184 + Offset);
    state.writer = Word(base, 0x886F4D10 + Offset);
    state.finished = Word(base, (state.serial & 1 ? 0x886F4CE4 : 0x886F4CF0) + Offset);
    state.published = Word(base, 0x888791A4 + Offset);
    state.cursor = Word(base, 0x886F4D0C + Offset);
    state.status = Word(base, 0x888791A8 + Offset);
    state.busy = Word(base, 0x886F4CCC + Offset);
    return state;
}
bool HasStack(const PPCContext& ctx, std::uint32_t reserve=0x400) {
    // The caller's frame is already committed. Keep the side-call's linkage
    // area in the same guest stack allocation (including its native save area).
    auto* memory = rex::system::kernel_state()->memory();
    const auto sp = ctx.r1.u32;
    if (sp < reserve) return false;
    auto* heap = memory->LookupHeap(sp);
    return heap && heap == memory->LookupHeap(sp - reserve) &&
        (std::uint32_t(heap->QueryRangeAccess(sp - reserve, sp)) & 3) == 3;
}
void Summary(const char* phase) {
    REXLOG_INFO("SDOJ_FRAME_DELAY module={} generation={} phase={} requested={} effective_policy={} publications={} late_receipts={} parks={} no_completion={} countdown={} metadata={} pending={} scene={} writer={} serial={} already_woken={} reader_active={} early_requests={} prefix_receipts={} driver_receipts={} timeouts={} slowdown_frames={} wait_max_us={} low_boundaries={} low_completed={} low_publications={} low_draining={} low_budget_skips={} low_state_skips={} low_control_skips={} low_policy_skips={} low_ownership_skips={} metrics_selected={} metrics_changed={} metrics_held={} low_metrics_skips={}",
        Module, handoff.generation(), phase, requested, effective, publications, receipts, parks,
        skipped[unsigned(Gate::NoCompletion)], skipped[unsigned(Gate::Countdown)],
        skipped[unsigned(Gate::Metadata)], skipped[unsigned(Gate::Pending)],
        skipped[unsigned(Gate::Scene)], skipped[unsigned(Gate::Writer)],
        skipped[unsigned(Gate::Serial)], skipped[unsigned(Gate::Wake)], skipped[unsigned(Gate::Reading)],
        requests, prefix_receipts, driver_receipts, timeouts, slow_frames, wait_max_us,
        low_boundaries, low_completed, low_publications, low_draining, low_budget_skips,
        low_state_skips, low_control_skips, low_policy_skips, low_ownership_skips,
        metrics_selected,metrics_changed,metrics_held,low_metrics_skips);
    if constexpr(Module==1)
        REXLOG_INFO("SDOJ_FRAME_DELAY_RECOVERY generation={} unparsed_skips={} deferred_wakes={} metrics_finalized={} metrics_native_changes={}",
            handoff.generation(),low_unparsed_skips,low_resyncs,metrics_finalized,metrics_native_changes);
}
}
extern "C" __declspec(dllexport) void ReXModule_OnLoad() {
    native_tick_timeouts=0;
    const auto tick_enabled=HostFunction<bool (*)()>("SdojNativeTickWaitEnabled");
    native_tick_wait.Start(tick_enabled && tick_enabled());
    using GetPolicy = unsigned (*)();
    const auto getter = reinterpret_cast<GetPolicy>(GetProcAddress(GetModuleHandleW(nullptr), "SdojFrameDelayPolicy"));
    std::lock_guard lock(gate);
    policy = getter ? getter() : 3;
    const auto wanted = HostFunction<GetPolicy>("SdojFrameDelayRequested");
    requested = wanted ? wanted() : policy; effective=policy;
    if (!handoff.Load(policy)) throw std::runtime_error("SDOJ module reloaded during a frame publication");
    skipped = {}; publications = receipts = parks = 0;
    requests=timeouts=slow_frames=prefix_receipts=driver_receipts=wait_max_us=0;
    low_boundaries=low_completed=low_publications=low_draining=low_budget_skips=0;
    low_state_skips=low_control_skips=low_policy_skips=low_ownership_skips=0;
    low_intermediate_yields=0; logic_thread=0;
    render_metrics.Reset();metrics_selected=metrics_changed=metrics_held=low_metrics_skips=0;
    low_unparsed_skips=low_resyncs=metrics_finalized=metrics_native_changes=0;draw_recovery.Reset();wrapper_metrics.reset();
    low.Load(handoff.generation()); prefix_result={};
    Summary("load");
    // The SDK may retain the PE mapping. Reset the optional observers for this
    // actual guest load too; their static bridge constructors are insufficient.
    using Sink = void (*)(std::uint32_t, std::uint32_t, std::uint32_t);
    for (const auto* name : {"SdojCoreInputEvent", "SdojRenderEvent", "SdojLatencyEvent"})
        if (const auto sink = reinterpret_cast<Sink>(GetProcAddress(GetModuleHandleW(nullptr), name))) sink(Module, 0, 0);
}
extern "C" __declspec(dllexport) void ReXModule_OnUnload() {
    native_tick_wait.Stop();
    std::lock_guard lock(gate);
    handoff.Retire(); low.Retire(); completion_changed.notify_all(); Summary("unload");
}
extern "C" __declspec(dllexport) void SdojFrameDelayStop() {
    native_tick_wait.Stop();
    std::lock_guard lock(gate);
    handoff.Retire(); low.Retire(); completion_changed.notify_all(); Summary("stop");
}

void SdojDelayLogic(PPCContext& ctx, std::uint8_t* base) {
#if SDOJ_MODULE_ID == 1
    SdojCoreNormalCopy(ctx.r27);
#else
    SdojCoreArrangeCopy(ctx.r25);
#endif
    if (policy == 3) return;
    std::lock_guard lock(gate); logic_thread=GetCurrentThreadId(); handoff.Logic(Snapshot(base));
}
void SdojDelayCommands(PPCContext& ctx, std::uint8_t* base) {
#if SDOJ_MODULE_ID == 1
    SdojLatencyNormalCommandsBegin(ctx.r11);
#else
    SdojLatencyArrangeCommandsBegin(ctx.r11);
#endif
    if (policy == 3) return;
    std::lock_guard lock(gate);
    handoff.Commands(ctx.r11.u32, Word(base, 0x886F4CD0 + Offset));
}
void SdojDelayPark(PPCContext& ctx, std::uint8_t* base) {
    static const auto audit_enabled=HostFunction<bool (*)()>("SdojLogicTraceEnabled");
    if (policy==3 && (!audit_enabled || !audit_enabled())) return;
    const auto caller=Word(base, ctx.r1.u32 + 136);
    if (policy==1 && caller==ResourceWaitReturn) {
        std::lock_guard lock(gate);
        // Only the known native resource retry loop on the actual logic worker.
        // It has captured its wake baseline but has not finished this frame.
        if (GetCurrentThreadId()==logic_thread &&
            handoff.ParkIntermediate(Snapshot(base),ctx.r29.u32,ctx.r30.u32)) {
            if (++low_intermediate_yields<=64)
                REXLOG_INFO("SDOJ_FRAME_DELAY module={} phase=resource_yield caller={:08X} logic={} baseline={} depth={} action=resume_native_ticks",Module,caller,handoff.logic(),ctx.r29.u32,ctx.r30.u32);
            completion_changed.notify_all();
        }
        return;
    }
    if (caller != WaitReturn) return;
    if (audit_enabled && audit_enabled()) {
        using Audit=void (*)(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
        static const auto audit=HostFunction<Audit>("SdojLogicParkEvent");
        if (audit) {
            const auto complete=Snapshot(base);
            audit(Module,complete.finished,complete.cursor,ctx.r30.u32);
        }
    }
    if (policy==3) return;
    std::lock_guard lock(gate);
    const auto native = Snapshot(base);
    // The cursor includes the sentinel. Guard the whole native 0x120000-byte
    // command allocation before dereferencing, not just the writer marker.
    const bool sentinel = native.finished && native.cursor >= native.finished + 4 &&
        std::uint64_t(native.cursor) <= std::uint64_t(native.finished) + 0x120000 &&
        !(native.cursor & 3) && Word(base, native.cursor - 4) == 0xffffffff;
    if (handoff.Park(native, ctx.r29.u32, ctx.r30.u32, sentinel)) {
        if constexpr(Module==1)
            render_metrics.Complete(handoff.logic(),native.finished,ctx.r29.u32,ctx.r30.u32);
        ++parks; completion_changed.notify_all();
    }
}
void SdojDelayUnpark(PPCContext&, std::uint8_t*) {
    if (policy != 3) { std::lock_guard lock(gate); handoff.Unpark(); }
#if SDOJ_MODULE_ID == 1
    SdojRenderNormalWaitEnd();
#else
    SdojRenderArrangeWaitEnd();
#endif
}
void SdojDelayRead(PPCContext& ctx, std::uint8_t*) {
    if (policy != 3) {
        std::lock_guard lock(gate); handoff.ReadBegin();
    }
#if SDOJ_MODULE_ID == 1
    SdojLatencyNormalSource(ctx.r4);
#else
    SdojLatencyArrangeSource(ctx.r4);
#endif
}
void SdojDelayMetricsParse(PPCContext& ctx, std::uint8_t*) {
    // The wrapper copies the published pointer every video frame, including
    // resource/slowdown holds. Only the parser's accepted-command branch is
    // evidence of a new sample; its zero-output early exit is not a reparse.
    if(policy!=3) {
        std::lock_guard lock(gate);render_metrics.BeginRead(ctx.r30.u32);
    }
    using Metrics=void (*)(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
    static const auto metrics=HostFunction<Metrics>("SdojLogicMetricsEvent");
    if(metrics)metrics(Module,0,ctx.r30.u32,0);
}
void SdojDelayRenderMetrics(PPCContext& ctx, std::uint8_t* base) {
    if(policy!=3) {
        std::lock_guard lock(gate);
        // ReadEnd pins identity, NOT values. Native presentation may clear
        // +544/+548 (88046514..88046520) after parsing, including on entry to
        // training. Only these original stores provide the final operands.
        if(handoff.active()) {
            const auto native=Snapshot(base);
            if(!wrapper_metrics || wrapper_metrics->renderer!=native.renderer)
                throw std::runtime_error("SDOJ Frame Delay: unmatched final render metrics wrapper");
            const RenderMetrics::Pair values{ctx.r11.u32,ctx.r10.u32};
            if(!render_metrics.Capture(wrapper_metrics->logic,values))
                throw std::runtime_error("SDOJ Frame Delay: inconsistent completed render metrics");
            if(wrapper_metrics->parser!=values && ++metrics_native_changes<=8) {
                const auto control=Word(base,0x888791DC);
                REXLOG_INFO("SDOJ_METRICS_FINALIZED logic={} frame={} calls={} parser={},{} stored={},{} control720={}",
                    wrapper_metrics->logic,native.frame,native.calls,wrapper_metrics->parser[0],wrapper_metrics->parser[1],
                    values[0],values[1],control ? Word(base,control+720) : 0);
            }
            if(wrapper_metrics->deferred) {
                const auto pending=*wrapper_metrics->deferred;
                if(native.frame!=pending.frame || native.calls!=pending.calls ||
                   !render_metrics.DeferWake(pending.frame,pending.calls))
                    throw std::runtime_error("SDOJ Frame Delay: recovery wake metrics mismatch");
                if(++low_resyncs<=8)
                    REXLOG_INFO("SDOJ_FRAME_DELAY module={} phase=resync_after_draw logic={} frame={} calls={} action=move_one_late_wake_to_next_early_slot count={}",
                        Module,wrapper_metrics->logic,native.frame,native.calls,low_resyncs);
            }
            ++metrics_finalized;
        }
        wrapper_metrics.reset();
    }
    using Metrics=void (*)(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
    static const auto metrics=HostFunction<Metrics>("SdojLogicMetricsEvent");
    if(metrics)metrics(Module,1,ctx.r11.u32,ctx.r10.u32);
}
void SdojDelaySlowdownInputs(PPCContext& ctx, std::uint8_t* base) {
    if(policy!=3) {
        std::lock_guard lock(gate);
        if(handoff.active()) {
            const auto selected=render_metrics.OriginalInputs();
            if(!selected) {
                const auto state=render_metrics.Inspect();
                const auto native=Snapshot(base);
                REXLOG_ERROR("SDOJ_METRICS_MISSING module={} logic={} missing={} consumed={} reading={} ticked={} tick_frame={} tick_calls={} frame={} calls={} countdown={} serial={} writer={:08X} finished={:08X} published={:08X} renderer={:08X} status={:08X} busy={}",
                    Module,handoff.logic(),state.missing,state.consumed,state.reading,state.ticked,state.frame,state.calls,
                    native.frame,native.calls,native.countdown,native.serial,native.writer,native.finished,native.published,
                    native.renderer,native.status,native.busy);
                for(const auto& entry:state.entries)
                    REXLOG_ERROR("SDOJ_METRICS_ENTRY logic={} buffer={:08X} due={} sampled={} values={},{}",
                        entry.logic,entry.buffer,entry.due,entry.sampled,entry.values[0],entry.values[1]);
                Summary("metrics_missing");
                rex::FlushLogging();
                throw std::runtime_error("SDOJ Frame Delay: missing original slowdown metrics at logic "+std::to_string(handoff.logic()));
            }
            ++metrics_selected;
            if(selected->values!=RenderMetrics::Pair{ctx.r9.u32,ctx.r10.u32})++metrics_changed;
            if(selected->held)++metrics_held;
            ctx.r9.u64=selected->values[0];ctx.r10.u64=selected->values[1];
        }
    }
    using Metrics=void (*)(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
    static const auto metrics=HostFunction<Metrics>("SdojLogicMetricsEvent");
    if(metrics)metrics(Module,2,ctx.r9.u32,ctx.r10.u32);
}
void SdojDelayReadEnd(PPCContext&, std::uint8_t* base) {
#if SDOJ_MODULE_ID == 1
    SdojLatencyNormalRenderEnd();
#else
    SdojLatencyArrangeRenderEnd();
#endif
    if (policy != 3) {
        std::lock_guard lock(gate);
        if constexpr(Module==1) {
            if(handoff.active()) {
                // Remember the accepted batch before later callbacks wake
                // the worker. Values remain provisional until native stores.
                const auto renderer=Word(base,0x888791D8);
                if(!renderer || wrapper_metrics)
                    throw std::runtime_error("SDOJ Frame Delay: unmatched render metrics wrapper");
                wrapper_metrics=WrapperMetrics{render_metrics.EndRead(),renderer,
                    {Word(base,renderer+544),Word(base,renderer+548)},std::nullopt};
            }
        }
        handoff.ReadEnd();
    }
}
static void Publish(PPCContext& ctx, std::uint8_t* base,bool current_complete) {
    if (policy == 3) return;
    std::optional<Publication> publication;
    {
        std::lock_guard lock(gate);
        if constexpr(Module==1) {
            const auto native=Snapshot(base);
            render_metrics.Tick(native.frame,native.calls);
        }
        if (low.SuppressWriter()) return;
        // Acquire the producer's completion before touching its command state.
        if (!handoff.completed()) { ++skipped[unsigned(Gate::NoCompletion)]; return; }
        const auto native = Snapshot(base);
        if (native.callback != Callback) return;
        const auto eligible = handoff.Eligible(native,current_complete);
        if (eligible != Gate::Ready) { ++skipped[unsigned(eligible)]; return; }
        if (!HasStack(ctx)) { ++skipped[unsigned(Gate::Metadata)]; return; }
        publication = handoff.BeginPublication(native,current_complete);
    }
    std::uint32_t result;
    {
        GuestCall call(ctx);
        rex::runtime::ResolveIndirectFunction(0x88033070)(ctx, base);
        result = ctx.r3.u32;
    }
    {
        std::lock_guard lock(gate);
        if (!handoff.Commit(*publication, Snapshot(base), result))
            throw std::runtime_error("SDOJ Frame Delay: native full-swap ownership mismatch");
        if (++publications % 600 == 1) Summary(effective == 1 ? "low" : "balanced");
    }
}
void SdojDelayPublish(PPCContext& ctx, std::uint8_t* base) { Publish(ctx,base,false); }
bool SdojDelayLateSwap(PPCContext& ctx, std::uint8_t* base) {
    if (policy == 3) return false;
    std::lock_guard lock(gate);
    if (!handoff.HasReceipt()) return false;
    const auto native = Snapshot(base);
    if (!handoff.Consume(native, Callback)) {
        REXLOG_ERROR("SDOJ_FRAME_DELAY late receipt mismatch module={} frame={} calls={} serial={} published={:08X}",
            Module, native.frame, native.calls, native.serial, native.published);
        throw std::runtime_error("SDOJ Frame Delay: unmatched late swap receipt");
    }
    ++receipts;
    ctx.r3.u64 = 1;
    ctx.lr = SwapReturn; // Reproduce the skipped BL's ABI, then original cmp/epilogue.
    return true;
}

// These hooks split only after the native callee's prologue and before its
// parser. Native epilogues restore that callee's actual linkage/FPR save area.
// The five live-through values are copied from the original prefix, not rebuilt
// with a different action mapper. r30's prefix-local stack pointer is dead at
// the cut: 880451DC loads the controller before the tail can use it.
bool SdojDelayInputStart(PPCContext& ctx, std::uint8_t* base) {
    if (input_only || policy != 1) return false;
    std::lock_guard lock(gate);
    if (!low.HasPrefix()) return false;
    if (!low.ConsumePrefix(Word(base,0x82633548),ctx.r3.u32) || !prefix_result.captured)
        throw std::runtime_error("SDOJ Frame Delay: input-prefix receipt mismatch");
    if (!prefix_result.Restore<Module>(ctx,base,ctx.r3.u32))
        throw std::runtime_error("SDOJ Frame Delay: frozen input ports changed before draw");
    ++prefix_receipts;
    return true;
}
bool SdojDelayInputDone(PPCContext& ctx, std::uint8_t* base) {
    if (!input_only) return false;
    prefix_result.Capture<Module>(ctx,base);
    return true;
}
bool SdojDelayDriverBegin(PPCContext& ctx, std::uint8_t* base) {
    if (early_driver || policy != 1) return false;
    std::lock_guard lock(gate);
    if (low.HasDriver()) {
        if (!low.ConsumeDriver(Word(base,0x82633548),Word(base,0x886B41EC+Offset)))
            throw std::runtime_error("SDOJ Frame Delay: native wake receipt mismatch");
        ++driver_receipts; ctx.r31.u64=1; return true;
    }
    // A timed-out writer still owns its buffer. Do not run its callback or
    // issue another wake while draining; the unrelated peripheral call runs.
    if (low.SuppressWriter()) { ctx.r31.u64=0; return true; }
    if constexpr(Module==1) {
        const auto native=Snapshot(base);
        const auto control=Word(base,0x888791DC+Offset);
        if (effective==1 && handoff.active() && !handoff.reading() && low.CanRequest() &&
            control && !Word(base,control+472) && wrapper_metrics &&
            render_metrics.CanResolve(wrapper_metrics->logic) &&
            draw_recovery.Defer(handoff.parked(),native,
                handoff.parked() && handoff.parked()->logic==wrapper_metrics->logic,handoff.HasReceipt())) {
            // No producer wake occurs here. Freeze its inputs only after
            // native presentation has finished and its actual stores execute.
            wrapper_metrics->deferred=WrapperMetrics::Deferred{native.frame,native.calls};
            ctx.r31.u64=0;return true; // The original peripheral tail still runs.
        }
        draw_recovery.Reset();
    }
    return false;
}
bool SdojDelayDriverPeripheral(PPCContext&, std::uint8_t*) {
    // This hook follows the original counter store, including an early driver.
    // A skipped tick may notify too; the original predicate still decides.
    native_tick_wait.Notify();
    return early_driver;
}

void SdojWaitNativeTick(PPCContext& ctx, std::uint8_t* base) {
    if (!native_tick_wait.Wait([&] {
            return !Word(base,ctx.r28.u32+4) && sdoj::performance::TickPending(
                Word(base,0x886B41EC+Offset),ctx.r29.u32,ctx.r30.s32);
        }) && native_tick_timeouts.fetch_add(1)==0)
        REXLOG_WARN("SDOJ_TICK_WAIT module={} fallback=original_poll reason=bounded_wait_failed recovery=next_original_producer",Module);
}
void SdojNativeTickInterrupted(PPCContext&, std::uint8_t*) { native_tick_wait.Notify(); }

extern "C" __declspec(dllexport) void SdojFrameDelayAdvance(PPCContext& ctx, std::uint8_t* base) {
    if (policy != 1) return;
    static const auto current_policy=HostFunction<unsigned (*)()>("SdojFrameDelayPolicy");
    std::optional<Request> request;
    std::uint64_t started{}, deadline{};
    {
        std::lock_guard lock(gate);
        if (!handoff.active()) return;
        draw_recovery.Reset();
        if constexpr(Module==1) {
            const auto native=Snapshot(base);
            render_metrics.Tick(native.frame,native.calls);
        }
        ++low_boundaries;
        effective=current_policy ? current_policy() : 2;
        if (low.SuppressWriter()) {
            ++low_draining;
            if (low.Recover(handoff.parked())) {
                REXLOG_INFO("SDOJ_FRAME_DELAY module={} phase=drained generation={} actual=balanced",Module,handoff.generation());
            } else if (low.RecoverIntermediate(handoff.intermediate())) {
                REXLOG_INFO("SDOJ_FRAME_DELAY module={} phase=drained_resource_wait generation={} actual=native_until_complete",Module,handoff.generation());
            }
            return; // Finish this boundary with the balanced path before retry.
        }
        if (effective!=1) { ++low_policy_skips; return; }
        if constexpr(Module==1) {
            // Never replace an unseen completed batch. The ordinary draw
            // captures it; a one-shot late-driver receipt may then move the
            // pending wake to the following early slot, recovering 1V without
            // remaining indefinitely on native-late (balanced) scheduling.
            if (low.CanRequest() && handoff.parked() && !handoff.reading() &&
                !render_metrics.Sampled(handoff.parked()->logic)) {
                const auto native=Snapshot(base);
                draw_recovery.Arm(*handoff.parked(),native);
                if (++low_unparsed_skips<=8)
                    REXLOG_INFO("SDOJ_FRAME_DELAY module={} phase=unparsed_completion logic={} frame={} calls={} action=draw_before_early_wake skips={}",
                        Module,handoff.parked()->logic,native.frame,native.calls,low_unparsed_skips);
                return;
            }
        }
        if constexpr(Module==1) {
            // The original late wake follows rendering. An unknown source
            // cannot be used for early logic yet: let the normal draw parse
            // it and resolve the frozen selection, then keep the late wake.
            // No early prefix/driver receipt has been created at this point.
            if (!render_metrics.OriginalInputs()) {
                if (++low_metrics_skips<=8) {
                    const auto state=render_metrics.Inspect();
                    REXLOG_INFO("SDOJ_FRAME_DELAY module={} phase=metrics_pending frame={} calls={} source={} action=render_before_native_late_wake",
                        Module,state.frame,state.calls,state.missing);
                }
                return;
            }
        }
        if (!low.CanRequest() || (!handoff.parked() && !handoff.intermediate()) || handoff.reading()) { ++low_state_skips; return; }
        const auto control=Word(base,0x888791DC+Offset);
        if (!control || Word(base,control+472) || !HasStack(ctx,0x1800)) { ++low_control_skips; return; }
    }
    // Bootstrap only the previous COMPLETE result. Existing late-swap receipts
    // are consumed by the early native callback, not lost or swapped twice.
    SdojDelayPublish(ctx,base);
    {
        std::lock_guard lock(gate);
        if ((!handoff.parked() && !handoff.intermediate()) || !handoff.active() || !low.CanRequest()) { ++low_state_skips; return; }
        const auto native=Snapshot(base);
        if (native.callback!=Callback) { ++low_control_skips; return; }
        started=Now();
        const auto budget=WaitBudget(started,rex::input::GuestVblankDeadlineMicros());
        if (budget < 1000) { ++low_budget_skips; return; }
        deadline=started+budget;
        request=handoff.parked() ? low.Begin(*handoff.parked(),native,rex::input::GuestInputFrame())
                                : low.BeginIntermediate(*handoff.intermediate(),native,rex::input::GuestInputFrame());
        if (!request) { ++low_ownership_skips; return; }
        prefix_result.captured=false;
    }
    {
        GuestCall call(ctx); FlagScope prefix(input_only);
        ctx.r3.u64=request->renderer;
        rex::runtime::ResolveIndirectFunction(0x88044960)(ctx,base);
    }
    {
        std::lock_guard lock(gate);
        if (!prefix_result.captured || !low.PrefixDone(*request))
            throw std::runtime_error("SDOJ Frame Delay: native input prefix did not complete");
    }
    std::uint32_t result;
    {
        GuestCall call(ctx); FlagScope drive(early_driver);
        rex::runtime::ResolveIndirectFunction(0x88030D90)(ctx,base);
        result=ctx.r3.u32;
    }
    std::unique_lock lock(gate);
    const auto after=Snapshot(base);
    if (!result && after.calls==request->calls) { low.CancelBeforeDrive(*request); return; }
    if (!low.DriveDone(*request,after,result))
        throw std::runtime_error("SDOJ Frame Delay: native callback/wake steps incomplete");
    ++requests;
    if (!request->wakes_writer) { ++slow_frames; return; }
    const auto limit=std::chrono::steady_clock::time_point(std::chrono::microseconds(deadline));
    const bool ready=completion_changed.wait_until(lock,limit,[&] {
        return !handoff.active() || low.IsComplete(*request,handoff.parked()) ||
               low.IsIntermediate(*request,handoff.intermediate());
    });
    const auto elapsed=Now()-started;
    wait_max_us=std::max(wait_max_us,elapsed);
    if (!handoff.active()) return;
    // Resource loading yields to subsequent native frame ticks. Waiting for a
    // complete draw while suppressing those ticks deadlocks the same worker.
    // Leave already-executed receipts intact. A following boundary can resume
    // this SAME update's wait early, so its eventual result is parsed before
    // another logic update. Untargeted/late waits still retain native ticks.
    if (ready && low.IsIntermediate(*request,handoff.intermediate())) return;
    const bool completed=ready && low.IsComplete(*request,handoff.parked());
    if (Trace() && requests <= 8192)
        REXLOG_INFO("SDOJ_FRAME_DELAY_REQUEST module={} generation={} request={} frame={} previous_logic={} calls={} input_epoch={} complete_epoch={} elapsed_us={} complete={} depth={} t_us={}",
            Module,request->generation,request->number,request->frame,request->previous_logic,
            request->calls,request->input_epoch,rex::input::GuestInputFrame(),elapsed,completed,
            handoff.parked() ? handoff.parked()->depth : 0,Now());
    if (!completed) {
        ++timeouts; low.Timeout(*request);
        lock.unlock();
        if (const auto timed_out=HostFunction<void (*)()>("SdojFrameDelayTimeout")) timed_out();
        return;
    }
    ++low_completed;
    if (handoff.parked()->depth>1) ++slow_frames;
    const auto previous_publications=publications;
    lock.unlock();
    Publish(ctx,base,true);
    lock.lock();
    if (publications!=previous_publications) ++low_publications;
}
