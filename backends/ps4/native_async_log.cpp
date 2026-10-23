#include "native_async_log.h"
#include "diagnostics/async_log_queue.h"
#include "diagnostics/monotonic_clock.h"
#include "diagnostics/timing_trace.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>

namespace {
using Queue = kt::diagnostics::AsyncLogQueue<>;
constexpr std::size_t kRecordBytes = 4096;
struct HostErrors {
    DWORD win = GetLastError();
    int crt = errno;
    ~HostErrors() { errno = crt; SetLastError(win); }
};
struct State {
    std::unique_ptr<Queue> queue;
    std::mutex stopping;
    std::atomic<std::uint64_t> format_failures{}, sink_wall_max_us{};
    std::atomic<bool> stop_requested{}, drain_completed{};
    std::uint64_t last_report_us{}; // Worker-only.

    bool PrintStats() noexcept {
        try {
            const auto stats = queue->Snapshot();
            return std::printf("{\"event\":\"native_async_log\",\"clock_us\":%llu,\"capacity_records\":1024,\"record_bytes\":4096,\"accepted\":%llu,\"written\":%llu,\"dropped_oldest\":%llu,\"contended\":%llu,\"invalid_records\":%llu,\"format_failures\":%llu,\"sink_failures\":%llu,\"after_stop\":%llu,\"pending\":%zu,\"in_flight\":%zu,\"sink_wall_max_us\":%llu,\"stop_requested\":%s,\"drain_completed\":%s}\n",
                kt::diagnostics::MonotonicMicroseconds(), stats.accepted, stats.written, stats.dropped_oldest,
                stats.contended, stats.oversized, format_failures.load(), stats.failures, stats.stopped,
                stats.pending, stats.in_flight, sink_wall_max_us.load(),
                stop_requested.load() ? "true" : "false", drain_completed.load() ? "true" : "false") >= 0;
        } catch (...) { ++format_failures; return false; }
    }
    static bool Write(void* context, std::string_view text) noexcept {
        auto& state = *static_cast<State*>(context);
        const auto began = kt::diagnostics::MonotonicMicroseconds();
        bool okay = std::fwrite(text.data(), 1, text.size(), stdout) == text.size();
        const auto completed = kt::diagnostics::MonotonicMicroseconds();
        if (completed - state.last_report_us >= 2000000) {
            state.last_report_us = completed;
            okay = state.PrintStats() && okay;
        }
        const auto duration = kt::diagnostics::MonotonicMicroseconds() - began;
        state.sink_wall_max_us.store((std::max)(state.sink_wall_max_us.load(), duration));
        return okay;
    }
    static bool Flush(void*) noexcept { return std::fflush(stdout) == 0; }
};
std::atomic<State*> active{};
}

bool StartNativeAsyncLog() {
    HostErrors preserve;
    if (active.load(std::memory_order_acquire)) return false;
    auto state = std::make_unique<State>();
    state->queue = std::make_unique<Queue>(State::Write, State::Flush, state.get());
    auto* expected = static_cast<State*>(nullptr);
    if (!active.compare_exchange_strong(expected, state.get(), std::memory_order_release)) return false;
    // Guest termination uses ExitProcess/TerminateProcess. Own this state for
    // the process lifetime; never run a live worker's destructor during teardown.
    (void)state.release();
    std::puts("{\"event\":\"native_async_log_begin\",\"bounded\":true,\"producer_file_io\":false,\"capacity_records\":1024,\"record_bytes\":4096}");
    if (kt::diagnostics::TimingTraceEnabled()) {
        LARGE_INTEGER qpc{}, frequency{}; QueryPerformanceCounter(&qpc); QueryPerformanceFrequency(&frequency);
        NativeLogf("{\"event\":\"native_timing_capture\",\"schema\":1,\"clock_us\":%llu,\"qpc\":%lld,\"qpc_frequency\":%lld,\"bounded_async_records\":1024,\"system_etw_available\":false,\"physical_scanout_measured\":false}\n",
            kt::diagnostics::MonotonicMicroseconds(),qpc.QuadPart,frequency.QuadPart);
    }
    return true;
}

bool WriteNativeLog(std::string_view text) noexcept {
    HostErrors preserve;
    if (auto* state = active.load(std::memory_order_acquire)) return state->queue->Submit(text);
    return std::fwrite(text.data(), 1, text.size(), stdout) == text.size();
}

int NativeLogf(const char* format, ...) noexcept {
    HostErrors preserve;
    std::va_list arguments; va_start(arguments, format);
    auto* state = active.load(std::memory_order_acquire);
    if (!state) {
        const auto result = std::vprintf(format, arguments); va_end(arguments); return result;
    }
    char buffer[kRecordBytes + 1];
    const auto size = std::vsnprintf(buffer, sizeof(buffer), format, arguments); va_end(arguments);
    if (size < 0) { ++state->format_failures; return -1; }
    if (!size) return 0;
    // Submit rejects an oversized view before reading it. Never publish a
    // truncated JSON fragment as a complete record.
    const auto bounded = (std::min)(static_cast<std::size_t>(size), sizeof(buffer));
    return state->queue->Submit({buffer, bounded}) ? size : -1;
}

bool StopNativeAsyncLog(std::chrono::milliseconds timeout) noexcept {
    HostErrors preserve;
    auto* state = active.load(std::memory_order_acquire);
    if (!state) return true;
    try {
        std::lock_guard lock(state->stopping);
        state->stop_requested = true;
        const bool drained = state->queue->Drain(timeout, true);
        state->drain_completed = drained;
        return drained;
    } catch (...) { return false; }
    // active stays published after stop: late producers drop/count rather
    // than silently falling back to synchronous file I/O during shutdown.
}

void PrintNativeAsyncLogDiagnostics() noexcept {
    HostErrors preserve;
    if (auto* state = active.load(std::memory_order_acquire)) (void)state->PrintStats();
}
