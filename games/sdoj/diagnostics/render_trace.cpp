#include "render_trace_policy.h"
#include "input_trace_policy.h"
#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/memory.h>
#include <rex/system/xmemory.h>
#include <rex/input/guest_input_frame.h>
#include <rex/logging.h>
#include <atomic>
#include <chrono>
#include <mutex>

REXCVAR_DEFINE_BOOL(sdoj_render_trace, false, "Diagnostics",
    "Bounded disc-base logic-copy, guest buffer publication and frame-wait observations")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
using namespace sdoj::diagnostics;
struct State {
    std::uint64_t generation{}, copies{}, copy_us{}, copy_frame{}, detail_until{};
    std::array<std::uint32_t, 2> masks{};
    std::array<std::uint64_t, 5> emitted{};
    std::array<bool, 5> has_emitted{};
};
std::array<State, 2> states;
std::mutex state_gate;
std::atomic<unsigned> records{};
std::atomic<std::uint64_t> contention{};
thread_local std::array<RenderWait, 2> waits;
constexpr unsigned MaxRecords = 2048;
std::uint64_t Now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool Admit() {
    auto n = records.load(std::memory_order_relaxed);
    while (n < MaxRecords)
        if (records.compare_exchange_weak(n, n + 1, std::memory_order_relaxed)) return true;
    return false;
}
std::span<const std::uint8_t> Read(std::uint32_t address, std::uint32_t size) {
    if (!address || !size || std::uint64_t(address) + size > 0x100000000ull) return {};
    auto* runtime = rex::Runtime::instance();
    auto* memory = runtime ? runtime->memory() : nullptr;
    auto* heap = memory ? memory->LookupHeap(address) : nullptr;
    if (!heap || heap != memory->LookupHeap(address + size - 1) ||
        !(std::uint32_t(heap->QueryRangeAccess(address, address + size - 1)) & 1)) return {};
    return {memory->TranslateVirtual(address), size};
}
}

// Read-only timing. A last-copy age is NOT lineage of the published buffer.
extern "C" __declspec(dllexport) void SdojRenderEvent(
    std::uint32_t module, std::uint32_t phase, std::uint32_t argument) {
    if (!REXCVAR_GET(sdoj_render_trace) || !CoreInputBlock(module) || phase > 4) return;
    auto& wait = waits[module - 1];
    const auto prior_wait = wait;
    if (phase == 3 || phase == 4) wait.active = false;
    std::unique_lock gate(state_gate, std::try_to_lock);
    if (!gate.owns_lock()) { ++contention; return; }
    auto& state = states[module - 1];
    if (!phase) {
        const auto next = state.generation + 1;
        state = {}; state.generation = next;
        return;
    }
    const auto now = Now(), frame = rex::input::GuestInputFrame();
    if (phase == 1) {
        if (argument != CoreInputBlock(module)) return;
        const auto sample = DecodeCoreInput(Read(argument, 176));
        if (!sample) return;
        if (!state.copies || state.masks != sample->current) state.detail_until = frame + 8;
        state.masks = sample->current;
        ++state.copies; state.copy_us = now; state.copy_frame = frame;
    }
    if (phase == 3) wait = {true, argument, state.generation, now, frame};
    const bool emit = !state.has_emitted[phase] || frame <= state.detail_until ||
        (frame >= state.emitted[phase] && frame - state.emitted[phase] >= 60);
    if (!emit) return;
    const auto addresses = RenderAddresses(module);
    std::array<std::uint32_t, 5> raw{};
    for (unsigned i = 0; i != raw.size(); ++i) {
        const auto word = RenderWord(Read(addresses[i], 4));
        if (!word) return;
        raw[i] = *word;
    }
    const auto second_serial = RenderWord(Read(addresses[1], 4));
    const bool stable_serial = second_serial && *second_serial == raw[1];
    const auto snapshot = state;
    state.emitted[phase] = frame; state.has_emitted[phase] = true;
    const bool paired = phase == 4 && prior_wait.Matches(state.generation, now, frame);
    gate.unlock();
    if (!Admit()) return;
    const auto age = SinceCoreBuild(snapshot.copy_us, snapshot.copy_frame, now, frame);
    REXLOG_INFO("SDOJ_RENDER t_us={} phase={} frame={} module={} generation={} copies={} last_copy_us={} last_copy_frame={} last_copy_age_us={} last_copy_age_frames={} current0={:08X} current1={:08X} countdown={} swap_serial={} writer={:08X} published={:08X} status={:08X} serial_stable={} wait_paired={} wait_depth={} wait_us={} wait_frames={} observer_contended={} limit={} no_guest_writes last_copy_not_buffer_lineage not_scanout_latency",
        now, phase == 1 ? "logic_copy" : phase == 2 ? "swap_return" : phase == 3 ? "wait_begin" : "wait_end",
        frame, module, snapshot.generation, snapshot.copies, snapshot.copy_us, snapshot.copy_frame,
        age ? std::int64_t(age->us) : -1, age ? std::int64_t(age->frames) : -1,
        snapshot.masks[0], snapshot.masks[1], raw[0], raw[1], raw[2], raw[3], raw[4],
        stable_serial, paired, phase == 3 ? argument : paired ? prior_wait.depth : 0,
        paired ? std::int64_t(now - prior_wait.us) : -1,
        paired ? std::int64_t(frame - prior_wait.frame) : -1, contention.load(), MaxRecords);
}
