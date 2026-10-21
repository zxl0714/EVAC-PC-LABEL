#include "input_trace_policy.h"
#include "render_trace_policy.h"
#include "logic_trace.h"
#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/memory.h>
#include <rex/system/xmemory.h>
#include <rex/input/guest_input_frame.h>
#include <rex/ui/flags.h>
#include <rex/logging.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <string>

// Diagnostic only: neither sampling, guest state nor frame ordering is changed.
REXCVAR_DEFINE_BOOL(sdoj_latency_trace, false, "Diagnostics",
    "Bounded disc-base command-buffer lineage and host-frame ordering observations")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(sdoj_latency_content, false, "Diagnostics",
    "Read-only bounded command snapshots around direction transitions; requires latency trace")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
std::atomic<std::uint64_t> sequence{}, host_iteration{}, rejected{};
std::atomic<std::uint64_t> render_source_sequence{}, render_source_iteration{};
std::atomic<bool> module_observed{};
constexpr std::uint64_t MaxRecords = 131072;
thread_local std::uint64_t native_dequeue_sequence{};
std::mutex gate;
struct Content {
    std::uint64_t generation{}, copy{}, batch{}, us{}, iteration{};
    std::uint32_t buffer{}, cursor{}, mask{};
    bool valid{};
    std::vector<std::uint32_t> words;
};
struct State {
    std::uint64_t generation{}, copies{}, batches{};
    std::uint32_t command_buffer{}, direction{}, content_left{}, content_triggers{};
    bool content_baseline{};
    Content previous;
};
std::array<State, 2> states;
std::uint64_t Now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::optional<std::uint32_t> Word(std::uint32_t address) {
    if (!address || address > 0xfffffffbu) return {};
    auto* runtime = rex::Runtime::instance();
    auto* memory = runtime ? runtime->memory() : nullptr;
    auto* heap = memory ? memory->LookupHeap(address) : nullptr;
    if (!heap || heap != memory->LookupHeap(address + 3) ||
        !(std::uint32_t(heap->QueryRangeAccess(address, address + 3)) & 1)) return {};
    return sdoj::diagnostics::RenderWord({memory->TranslateVirtual(address), 4});
}
std::uint32_t Value(std::uint32_t address) { return Word(address).value_or(0); }
Content ReadContent(const State& state, std::uint32_t cursor, std::uint32_t mask) {
    Content result{state.generation, state.copies, state.batches, Now(),
        host_iteration.load(), state.command_buffer, cursor, mask};
    // The original writer's end cursor includes its sentinel. Reject partial,
    // oversized or unmapped buffers rather than silently truncating a snapshot.
    if (!result.buffer || cursor <= result.buffer || (cursor - result.buffer) % 4 ||
        cursor - result.buffer > 65536) return result;
    auto* runtime = rex::Runtime::instance();
    auto* memory = runtime ? runtime->memory() : nullptr;
    auto* heap = memory ? memory->LookupHeap(result.buffer) : nullptr;
    if (!heap || heap != memory->LookupHeap(cursor - 1) ||
        !(std::uint32_t(heap->QueryRangeAccess(result.buffer, cursor - 1)) & 1)) return result;
    auto* bytes = memory->TranslateVirtual<std::uint8_t*>(result.buffer);
    result.words.reserve((cursor - result.buffer) / 4);
    for (std::uint32_t offset = 0; offset != cursor - result.buffer; offset += 4)
        result.words.push_back(*sdoj::diagnostics::RenderWord({bytes + offset, 4}));
    result.valid = !result.words.empty() && result.words.back() == 0xffffffff;
    return result;
}
void LogContent(unsigned module, const Content& content, bool baseline) {
    // Fixed-size chunks keep records below the logger's line-size limit. The
    // declared total, offsets and metadata allow offline completeness checks.
    const auto count = content.words.size();
    for (std::size_t offset = 0; offset < std::max<std::size_t>(1, count); offset += 64) {
        std::string words;
        for (auto index = offset; index < std::min(offset + 64, count); ++index)
            words += fmt::format("{:08X}", content.words[index]);
        REXLOG_INFO("SDOJ_CONTENT module={} generation={} copy={} batch={} t_us={} host_iteration={} buffer={:08X} cursor={:08X} mask={:08X} baseline={} valid={} total={} offset={} words={} not_pixels_or_scanout",
            module, content.generation, content.copy, content.batch, content.us,
            content.iteration, content.buffer, content.cursor, content.mask,
            baseline, content.valid, count, offset, words.empty() ? "-" : words);
    }
}
}

// Phases 1..4 reuse the reviewed copy / swap / wait sites. 5..8 observe the
// command writer and the renderer's actual source argument, not the next swap.
extern "C" __declspec(dllexport) void SdojLatencyEvent(
    std::uint32_t module, std::uint32_t phase, std::uint32_t argument) {
    SdojLogicTraceEvent(module,phase,argument);
    if (!REXCVAR_GET(sdoj_latency_trace) || module < 1 || module > 2 || phase > 10) return;
    // Waiting at the title must not spend the entire gameplay trace budget.
    module_observed.store(true, std::memory_order_relaxed);
    const auto serial = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if (serial > MaxRecords) return;
    std::unique_lock lock(gate, std::try_to_lock);
    if (!lock.owns_lock()) { ++rejected; return; }
    auto& state = states[module - 1];
    if (!phase) { const auto generation = state.generation + 1; state = {}; state.generation = generation; }
    if (phase == 1) ++state.copies;
    if (phase == 5) ++state.batches;
    const auto addresses = sdoj::diagnostics::RenderAddresses(module);
    const auto swap_before = Word(addresses[1]);
    std::array<std::uint32_t, 5> raw{};
    bool valid = true;
    for (unsigned i = 0; i != raw.size(); ++i) {
        const auto word = Word(addresses[i]);
        valid = valid && bool(word);
        raw[i] = word.value_or(0);
    }
    const auto block = sdoj::diagnostics::CoreInputBlock(module);
    const auto current0 = Value(block + 12), current1 = Value(block + 172);
    const auto consumed0 = Value(block), consumed1 = Value(block + 160);
    // In each independently reviewed layout, command cursor immediately
    // precedes active_writer. 0x82633548 is this disc's engine-frame counter.
    const auto cursor = Value(addresses[2] - 4);
    const auto engine_frame = Value(0x82633548);
    const auto swap_after = Word(addresses[1]);
    const auto snapshot = state;
    const auto now = Now(), epoch = rex::input::GuestInputFrame();
    const auto iteration = host_iteration.load(std::memory_order_relaxed);
    const std::uint32_t render_object = module == 1 ? 0x885F1A70u : 0x885F97F8u;
    const auto draw_list = phase == 7 ? Value(render_object + 900) : 0;
    if (phase == 7) {
        render_source_iteration.store(iteration, std::memory_order_relaxed);
        render_source_sequence.store(serial, std::memory_order_relaxed);
    }
    std::vector<Content> content;
    bool with_baseline = false;
    if (REXCVAR_GET(sdoj_latency_content)) {
        if (phase == 1) {
            const auto direction = current0 & 15u;
            if (direction && direction != state.direction && state.content_triggers < 8) {
                ++state.content_triggers;
                state.content_left = 4;
                state.content_baseline = true;
            }
            state.direction = direction;
        }
        if (phase == 5) state.command_buffer = argument;
        if (phase == 6) {
            auto current = ReadContent(state, cursor, current0);
            if (state.content_left) {
                if (state.content_baseline && state.previous.valid) {
                    content.push_back(state.previous);
                    with_baseline = true;
                }
                content.push_back(current);
                --state.content_left;
                state.content_baseline = false;
            }
            state.previous = std::move(current);
        }
    }
    lock.unlock();
    constexpr const char* names[] = {"module_reset", "logic_copy", "swap_return",
        "wait_begin", "wait_end", "commands_begin", "commands_end",
        "render_source", "render_return", "callback_begin", "callback_end"};
    REXLOG_INFO("SDOJ_LATENCY seq={} t_us={} thread={} phase={} module={} generation={} host_iteration={} engine_frame={} epoch={} copies={} batches={} argument={:08X} current0={:08X} current1={:08X} consumed0={:08X} consumed1={:08X} countdown={} swap_serial={} writer={:08X} published={:08X} status={:08X} cursor={:08X} valid={} swap_stable={} rejected={} limit={} draw_list={:08X} no_guest_writes not_scanout_latency",
        serial, now, GetCurrentThreadId(), names[phase], module, snapshot.generation,
        iteration, engine_frame, epoch, snapshot.copies, snapshot.batches, argument,
        current0, current1, consumed0, consumed1, raw[0], raw[1], raw[2], raw[3],
        raw[4], cursor, valid, swap_before && swap_after && *swap_before == raw[1] && *swap_after == raw[1],
        rejected.load(std::memory_order_relaxed), MaxRecords, draw_list);
    for (std::size_t i = 0; i < content.size(); ++i) LogContent(module, content[i], with_baseline && i == 0);
}

void SdojLatencyHostBegin() {
    if (!REXCVAR_GET(sdoj_latency_trace)) return;
    const auto iteration = host_iteration.fetch_add(1, std::memory_order_relaxed) + 1;
    render_source_sequence.store(0, std::memory_order_relaxed);
    if (!module_observed.load(std::memory_order_relaxed)) return;
    const auto serial = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if (serial <= MaxRecords) REXLOG_INFO(
        "SDOJ_LATENCY seq={} t_us={} thread={} phase=host_begin host_iteration={} engine_frame={} epoch={} rejected={} limit={} no_guest_writes not_scanout_latency",
        serial, Now(), GetCurrentThreadId(), iteration, Value(0x82633548),
        rex::input::GuestInputFrame(), rejected.load(), MaxRecords);
}
void SdojLatencyHostEnd() {
    if (!REXCVAR_GET(sdoj_latency_trace)) return;
    if (!module_observed.load(std::memory_order_relaxed)) return;
    const auto serial = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if (serial <= MaxRecords) REXLOG_INFO(
        "SDOJ_LATENCY seq={} t_us={} thread={} phase=host_end host_iteration={} engine_frame={} epoch={} rejected={} limit={} no_guest_writes not_scanout_latency",
        serial, Now(), GetCurrentThreadId(), host_iteration.load(), Value(0x82633548),
        rex::input::GuestInputFrame(), rejected.load(), MaxRecords);
}

void SdojLatencySubmit(PPCRegister& buffer) {
    if (!REXCVAR_GET(sdoj_latency_trace) || !module_observed.load()) return;
    const auto serial = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if (serial > MaxRecords) return;
    auto* runtime = rex::Runtime::instance();
    auto* memory = runtime ? runtime->memory() : nullptr;
    const auto physical = memory ? memory->GetPhysicalAddress(buffer.u32) : 0xffffffffu;
    const bool valid = physical != 0xffffffffu && physical <= 0xffffffe3u;
    const auto iteration = host_iteration.load();
    const auto source = render_source_iteration.load() == iteration ? render_source_sequence.load() : 0;
    REXLOG_INFO("SDOJ_LATENCY seq={} t_us={} thread={} phase=guest_submit host_iteration={} engine_frame={} epoch={} render_source_seq={} native_dequeue_seq={} argument={:08X} packet={:08X} valid={} output_trace_enabled={} rejected={} limit={} no_guest_writes not_scanout_latency",
        serial, Now(), GetCurrentThreadId(), iteration, Value(0x82633548), rex::input::GuestInputFrame(),
        source, native_dequeue_sequence, buffer.u32, valid ? physical + 28 : 0, valid,
        REXCVAR_GET(output_latency_trace), rejected.load(), MaxRecords);
}

namespace {
void NativeQueue(bool enqueue, std::uint32_t queue, std::uint32_t slot,
                 std::uint32_t output = 0) {
    if (!REXCVAR_GET(sdoj_latency_trace) || !module_observed.load()) return;
    const auto serial = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if (serial > MaxRecords) return;
    const auto kind = Word(output ? output : slot), argument = Word((output ? output : slot) + 4);
    const auto base = Word(queue + 4), capacity = Word(queue + 8);
    const auto read = Word(queue + 16), write = Word(queue + 12);
    const bool valid = kind && argument && base && capacity && read && write &&
        *capacity && *capacity <= 4096 && *read < *capacity && *write < *capacity &&
        slot >= *base && std::uint64_t(slot) + 8 <= std::uint64_t(*base) + *capacity * 8 &&
        (slot - *base) % 8 == 0;
    const auto iteration = host_iteration.load();
    const auto source = enqueue && render_source_iteration.load() == iteration
        ? render_source_sequence.load() : 0;
    if (!enqueue) native_dequeue_sequence = valid && *kind == 1 ? serial : 0;
    REXLOG_INFO("SDOJ_LATENCY seq={} t_us={} thread={} phase={} host_iteration={} engine_frame={} epoch={} render_source_seq={} queue={:08X} slot={:08X} kind={} frame_arg={:08X} capacity={} read={} write={} valid={} rejected={} limit={} no_guest_writes not_scanout_latency",
        serial, Now(), GetCurrentThreadId(), enqueue ? "native_enqueue" : "native_dequeue",
        iteration, Value(0x82633548), rex::input::GuestInputFrame(), source, queue, slot,
        kind.value_or(0), argument.value_or(0), capacity.value_or(0), read.value_or(0), write.value_or(0),
        valid, rejected.load(), MaxRecords);
}
}
void SdojLatencyNativeEnqueue(PPCRegister& queue, PPCRegister& offset, PPCRegister& base) {
    if (!REXCVAR_GET(sdoj_latency_trace)) return;
    const auto slot = std::uint64_t(base.u32) + offset.u32;
    NativeQueue(true, queue.u32, slot <= 0xfffffff7u ? std::uint32_t(slot) : 0);
}
void SdojLatencyNativeDequeue(PPCRegister& queue, PPCRegister& output) {
    if (!REXCVAR_GET(sdoj_latency_trace)) return;
    const auto capacity = Word(queue.u32 + 8), read = Word(queue.u32 + 16), base = Word(queue.u32 + 4);
    const auto index = capacity && read && *capacity && *capacity <= 4096 && *read < *capacity
        ? (*read + *capacity - 1) % *capacity : 0;
    const auto slot = base ? std::uint64_t(*base) + index * 8 : 0;
    NativeQueue(false, queue.u32, slot <= 0xfffffff7u ? std::uint32_t(slot) : 0, output.u32);
}

namespace {
void NativeWait(const char* phase) {
    if (!REXCVAR_GET(sdoj_latency_trace) || !module_observed.load()) return;
    const auto serial = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if (serial > MaxRecords) return;
    // Original callback sub_8219CCF0 clears 0x8279C770. The begin hook is
    // immediately BEFORE the original store of one; it does not set the flag.
    REXLOG_INFO("SDOJ_LATENCY seq={} t_us={} thread={} phase={} host_iteration={} engine_frame={} epoch={} native_dequeue_seq={} wait_flag={} rejected={} limit={} no_guest_writes not_scanout_latency",
        serial, Now(), GetCurrentThreadId(), phase, host_iteration.load(),
        Value(0x82633548), rex::input::GuestInputFrame(), native_dequeue_sequence,
        Value(0x8279C770), rejected.load(), MaxRecords);
}
}
void SdojLatencyNativeWaitBegin() { NativeWait("native_wait_begin"); }
void SdojLatencyNativeWaitEnd() { NativeWait("native_wait_end"); }
