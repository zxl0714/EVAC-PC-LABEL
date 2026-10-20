#include "input_trace_policy.h"
#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/memory.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/input/guest_input_frame.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>

REXCVAR_DEFINE_BOOL(sdoj_input_trace, false, "Diagnostics",
    "Bounded disc-base input polling/copy observations; does not change sampling")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
using namespace sdoj::diagnostics;
thread_local InputPass pass;
thread_local std::array<std::optional<PadSample>, 4> previous;
thread_local std::array<std::optional<std::uint16_t>, 4> copied;
thread_local std::uint64_t last_summary_frame = ~std::uint64_t{};
std::atomic<unsigned> records{};
constexpr unsigned MaxRecords = 2048;
struct CoreTrace {
    std::uint64_t generation{}, built_us{}, built_frame{}, serial{}, copies{};
    std::array<std::uint64_t, 5> ages{};
    std::uint64_t unpaired{};
    std::array<std::uint32_t, 2> built{};
    std::array<std::optional<std::array<std::uint32_t, 2>>, 2> emitted;
    std::array<std::uint64_t, 2> emitted_frame{};
};
std::array<CoreTrace, 2> core_trace;
std::mutex core_trace_gate;
std::atomic<std::uint64_t> core_contention{};
std::uint64_t Now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool Admit() {
    auto n = records.load(std::memory_order_relaxed);
    while (n < MaxRecords) {
        if (records.compare_exchange_weak(n, n + 1, std::memory_order_relaxed)) return true;
    }
    return false;
}
std::span<const std::uint8_t> ReadRange(std::uint32_t address, std::uint32_t size) {
    if (!size || !address || std::uint64_t(address) + size > 0x100000000ull) return {};
    auto* runtime = rex::Runtime::instance();
    auto* memory = runtime ? runtime->memory() : nullptr;
    auto* heap = memory ? memory->LookupHeap(address) : nullptr;
    if (!heap || heap != memory->LookupHeap(address + size - 1) ||
        !(std::uint32_t(heap->QueryRangeAccess(address, address + size - 1)) & 1)) return {};
    return {memory->TranslateVirtual(address), size};
}
std::optional<PadSample> ReadPad(std::uint32_t address, std::uint32_t status) {
    return status ? std::optional<PadSample>{} : DecodePad(ReadRange(address, 16), status);
}
void Poll(std::uint32_t status, std::uint32_t user, std::uint32_t slot) {
    if (user >= previous.size()) return;
    if (pass.active) ++pass.polls;
    const auto value = std::uint64_t(slot) + 516 <= 0xffffffffull
        ? ReadPad(slot + 516, status) : std::optional<PadSample>{};
    const bool changed = bool(value) != bool(previous[user]) ||
        (value && previous[user] && !value->SameControls(*previous[user]));
    previous[user] = value;
    if (!changed) return; // Packet-only churn must not consume the trace budget.
    pass.changed = true;
    if (!Admit()) return;
    REXLOG_INFO("SDOJ_INPUT t_us={} phase=poll_result frame={} user={} status={} valid={} packet={} buttons={:04X} lt={} rt={} lx={:04X} ly={:04X} rx={:04X} ry={:04X} limit={} no_guest_writes",
        Now(), rex::input::GuestInputFrame(), user, status, bool(value),
        value ? value->packet : 0, value ? value->buttons : 0,
        value ? value->left_trigger : 0, value ? value->right_trigger : 0,
        value ? value->axes[0] : 0, value ? value->axes[1] : 0,
        value ? value->axes[2] : 0, value ? value->axes[3] : 0, MaxRecords);
}
}

// Observation hooks only: no register writes, alternate polling or guest calls.
void SdojInputPassBegin(PPCRegister& object) {
    if (!REXCVAR_GET(sdoj_input_trace)) return;
    pass.Begin(object.u32, Now(), rex::input::GuestInputFrame());
}
void SdojInputPollExisting(PPCRegister& status, PPCRegister& user, PPCRegister& slot) {
    if (REXCVAR_GET(sdoj_input_trace)) Poll(status.u32, user.u32, slot.u32);
}
void SdojInputPollDiscovery(PPCRegister& status, PPCRegister& user, PPCRegister& slot) {
    if (REXCVAR_GET(sdoj_input_trace)) Poll(status.u32, user.u32, slot.u32);
}
void SdojInputButtonCopy(PPCRegister& buttons, PPCRegister& user) {
    if (!REXCVAR_GET(sdoj_input_trace) || user.u32 >= copied.size()) return;
    const auto value = buttons.u16;
    if (copied[user.u32] == value) return;
    copied[user.u32] = value; pass.changed = true;
    if (Admit()) REXLOG_INFO("SDOJ_INPUT t_us={} phase=button_copy frame={} user={} buttons={:04X} no_guest_writes",
        Now(), rex::input::GuestInputFrame(), user.u32, value);
}
void SdojInputPassEnd(PPCRegister& manager) {
    if (!REXCVAR_GET(sdoj_input_trace)) return;
    const auto now = Now(), frame = rex::input::GuestInputFrame();
    const bool valid = pass.MatchesEnd(manager.u32, now);
    if (valid && (pass.changed || last_summary_frame == ~std::uint64_t{} ||
                  (frame >= last_summary_frame && frame - last_summary_frame >= 60))) {
        last_summary_frame = frame;
        if (Admit()) REXLOG_INFO("SDOJ_INPUT t_us={} phase=engine_input_pass frame={} began_frame={} began_us={} elapsed_us={} polls={} changed={} no_guest_writes not_gameplay_or_scanout_latency",
            now, frame, pass.began_frame, pass.began_us, now - pass.began_us, pass.polls, pass.changed);
    }
    pass.active = false;
}

extern "C" __declspec(dllexport) void SdojCoreInputEvent(
    std::uint32_t module, std::uint32_t phase, std::uint32_t block) {
    if (!REXCVAR_GET(sdoj_input_trace) || !CoreInputBlock(module) || phase > 2) return;
    // Never wait for a diagnostic observer on either guest thread.
    std::unique_lock gate(core_trace_gate, std::try_to_lock);
    if (!gate.owns_lock()) { ++core_contention; return; }
    auto& state = core_trace[module - 1];
    if (!phase) {
        const auto next = state.generation + 1;
        state = {}; state.generation = next;
        return;
    }
    if (block != CoreInputBlock(module)) return;
    const auto sample = DecodeCoreInput(ReadRange(block, 176));
    if (!sample) return;
    const auto now = Now(), frame = rex::input::GuestInputFrame();
    if (phase == 1) {
        state.built = sample->current; state.built_us = now;
        state.built_frame = frame; ++state.serial;
    }
    const auto age = SinceCoreBuild(state.built_us, state.built_frame, now, frame);
    const bool same_masks = age && state.built == sample->current;
    if (phase == 2) {
        ++state.copies;
        if (same_masks) ++state.ages[std::min<std::uint64_t>(age->frames, 4)];
        else ++state.unpaired;
    }
    const auto slot = phase - 1;
    const bool emit = !state.emitted[slot] || *state.emitted[slot] != sample->current ||
        (frame >= state.emitted_frame[slot] && frame - state.emitted_frame[slot] >= 60);
    if (emit) { state.emitted[slot] = sample->current; state.emitted_frame[slot] = frame; }
    const auto snapshot = state;
    gate.unlock(); // Formatting and the logger never run under the observer gate.
    if (!emit || !Admit()) return;
    REXLOG_INFO("SDOJ_INPUT t_us={} phase={} frame={} module={} generation={} build_serial={} built_us={} built_frame={} since_build_us={} since_build_frames={} same_masks={} previous0={:08X} previous1={:08X} current0={:08X} current1={:08X} copies={} ages_frames={},{},{},{},{} unpaired={} observer_contended={} no_guest_writes not_scanout_latency",
        now, phase == 1 ? "core_mask_built" : "core_mask_copy", frame, module,
        snapshot.generation, snapshot.serial, snapshot.built_us, snapshot.built_frame,
        age ? std::int64_t(age->us) : -1, age ? std::int64_t(age->frames) : -1, same_masks,
        sample->previous[0], sample->previous[1], sample->current[0], sample->current[1],
        snapshot.copies, snapshot.ages[0], snapshot.ages[1], snapshot.ages[2],
        snapshot.ages[3], snapshot.ages[4], snapshot.unpaired, core_contention.load());
}
