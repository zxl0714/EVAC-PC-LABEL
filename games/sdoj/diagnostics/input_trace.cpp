#include "input_trace_policy.h"
#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/memory.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/input/guest_input_frame.h>
#include <array>
#include <atomic>
#include <chrono>

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
std::optional<PadSample> ReadPad(std::uint32_t address, std::uint32_t status) {
    if (status || !address || std::uint64_t(address) + 16 > 0x100000000ull) return {};
    auto* runtime = rex::Runtime::instance();
    auto* memory = runtime ? runtime->memory() : nullptr;
    auto* heap = memory ? memory->LookupHeap(address) : nullptr;
    if (!heap || heap != memory->LookupHeap(address + 15) ||
        !(std::uint32_t(heap->QueryRangeAccess(address, address + 15)) & 1)) return {};
    return DecodePad({memory->TranslateVirtual(address), 16}, status);
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
