#pragma once
#include "host_services.h"
#include "diagnostics/timing_trace.h"
#include <array>

namespace Ketsui::GpuCore {
// Opt-in CPU-side wall intervals, accumulated on the serial GPU worker. Emit
// once per batch, never once per draw. Program reads are a nested breakdown of
// pipeline work and must not be added to the mutually exclusive draw stages.
struct DrawTimingTotals {
    std::array<std::uint64_t, 7> stages{};
    std::uint64_t attempts{}, program_reads{}, program_us{};
};
inline thread_local DrawTimingTotals draw_timing_totals;

class DrawTimingScope {
    std::uint64_t last = kt::diagnostics::TimingTraceEnabled()
        ? kt::diagnostics::MonotonicMicroseconds() : 0;
    unsigned stage{};
public:
    DrawTimingScope() { if (last) ++draw_timing_totals.attempts; }
    ~DrawTimingScope() { Next(stage); }
    void Next(unsigned next) {
        if (!last) return;
        const auto now = kt::diagnostics::MonotonicMicroseconds();
        draw_timing_totals.stages[stage] += now - last;
        last = now;
        stage = next;
    }
};
class ProgramReadTimingScope {
    std::uint64_t began = kt::diagnostics::TimingTraceEnabled()
        ? kt::diagnostics::MonotonicMicroseconds() : 0;
public:
    ~ProgramReadTimingScope() {
        if (!began) return;
        ++draw_timing_totals.program_reads;
        draw_timing_totals.program_us += kt::diagnostics::MonotonicMicroseconds() - began;
    }
};
inline void ReportDrawTiming(std::uint64_t tick) {
    if (!kt::diagnostics::TimingTraceEnabled()) return;
    const auto t = draw_timing_totals;
    draw_timing_totals = {};
    if (!t.attempts) return;
    TimingLog("{\"event\":\"native_gpu_draw_breakdown\",\"clock_us\":%llu,\"tick\":%llu,\"attempts\":%llu,"
        "\"pending_filter_us\":%llu,\"pipeline_us\":%llu,\"prepare_state_us\":%llu,"
        "\"resources_us\":%llu,\"render_state_us\":%llu,\"vertices_us\":%llu,\"emit_reset_us\":%llu,"
        "\"nested_program_reads\":%llu,\"nested_program_us\":%llu}\n",
        kt::diagnostics::MonotonicMicroseconds(), tick, t.attempts,
        t.stages[0], t.stages[1], t.stages[2], t.stages[3], t.stages[4], t.stages[5], t.stages[6],
        t.program_reads, t.program_us);
}
}
