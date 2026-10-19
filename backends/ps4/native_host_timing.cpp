#include "native_host_timing.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace {
constexpr std::uint64_t kThresholdUs = 8000;
constexpr unsigned kPerThreadSecond = 8, kProcessSecond = 64;
std::uint64_t Microseconds(std::chrono::steady_clock::time_point value) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(value.time_since_epoch()).count());
}
struct ThreadBudget { std::uint64_t second = UINT64_MAX; unsigned count{}; };
struct Budget {
    unsigned thread_limit = kPerThreadSecond, process_limit = kProcessSecond;
    // A single CAS publishes the second and its count. A stale caller must not
    // reset a newer window or admit more records by moving the clock backward.
    std::atomic<std::uint64_t> window{}, emitted{}, suppressed{}, pending{}, failures{};
    bool Admit(std::uint64_t began, std::uint64_t completed, ThreadBudget& local) noexcept {
        if (completed < began || completed - began < kThresholdUs) { return false; }
        const auto second = completed / 1000000;
        if (local.second == UINT64_MAX || second > local.second) { local.second = second; local.count = 0; }
        if (second < local.second || local.count >= thread_limit) { Reject(); return false; }
        auto observed = window.load(std::memory_order_relaxed);
        for (;;) {
            const auto previous = observed >> 7;
            const auto count = previous == second ? static_cast<unsigned>(observed & 127) : 0;
            if (previous > second || count >= process_limit) { Reject(); return false; }
            const auto next = (second << 7) | (count + 1);
            if (window.compare_exchange_weak(observed, next, std::memory_order_relaxed)) {
                ++local.count; emitted.fetch_add(1, std::memory_order_relaxed); return true;
            }
        }
    }
    void Reject() noexcept { suppressed.fetch_add(1, std::memory_order_relaxed); pending.fetch_add(1, std::memory_order_relaxed); }
};
// Normal frame/event waits cannot consume all useful-work diagnostics. Waits
// at least 25 ms join the useful-work pool, so a new long wait stays observable.
Budget budget{8, 48}, routine_wait_budget{2, 16};
thread_local ThreadBudget thread_budget, routine_wait_thread_budget;
using Clock = std::chrono::steady_clock;
constexpr std::array<const char*, 6> category_names{"allocation", "memory", "io", "synchronization", "sleep", "fixture"};
constexpr std::array<const char*, 10> allocation_names{"unclassified", "new", "new_nothrow", "malloc", "memalign", "free_delete",
    "mspace_create", "mspace_destroy", "mspace_memalign", "mspace_free"};
struct Counter {
    std::uint64_t calls{};
    Clock::duration total{}, maximum{};
};
struct Counters {
    std::array<Counter, category_names.size()> categories{};
    std::array<Counter, allocation_names.size()> allocations{};
    Counter outer;
    std::uint64_t cross_boundary{};
    void Add(unsigned category, Clock::duration duration, bool is_outer, NativeHostAllocationKind allocation = NativeHostAllocationKind::Unclassified) noexcept {
        auto add = [&](Counter& counter) {
            if (counter.calls != UINT64_MAX) { ++counter.calls; }
            const auto maximum = Clock::duration::max();
            counter.total = duration > maximum - counter.total ? maximum : counter.total + duration;
            counter.maximum = (std::max)(counter.maximum, duration);
        };
        // Exactly one category/subcategory counter per call. Derive the whole
        // allocation category at frame output, rather than charging another
        // counter update to high-frequency allocation paths.
        const auto index = static_cast<unsigned>(allocation);
        if (category == 0) { add(allocations[index < allocations.size() ? index : 0]); }
        else { add(categories[category]); }
        if (is_outer) { add(outer); }
    }
    Counter Category(unsigned index) const noexcept {
        if (index != 0) { return categories[index]; }
        Counter result;
        for (const auto& value : allocations) {
            result.calls = value.calls > UINT64_MAX - result.calls ? UINT64_MAX : result.calls + value.calls;
            const auto maximum = Clock::duration::max();
            result.total = value.total > maximum - result.total ? maximum : result.total + value.total;
            result.maximum = (std::max)(result.maximum, value.maximum);
        }
        return result;
    }
};
struct ThreadCounters {
    Counters values;
    std::uint64_t depth{}, timed_depth{}, previous_clock{}, previous_submission{};
    bool previous_active{};
};
constinit thread_local ThreadCounters frame_counters;
struct TimingMode {
    std::atomic<unsigned> value{2}; // full=1, counts=2 (default), frozen=4
    bool Configure(bool full) noexcept {
        auto observed = value.load(std::memory_order_acquire);
        do {
            if (observed & 4) { return false; }
        } while (!value.compare_exchange_weak(observed, full ? 1u : 2u, std::memory_order_acq_rel));
        return true;
    }
    bool AcquireFull() noexcept {
        auto observed = value.load(std::memory_order_acquire);
        while (!(observed & 4) && !value.compare_exchange_weak(observed, observed | 4, std::memory_order_acq_rel)) {}
        return (observed & 3) != 2;
    }
    bool Full() const noexcept { return (value.load(std::memory_order_acquire) & 3) != 2; }
};
TimingMode allocation_timing;
std::atomic<std::uint64_t> frame_emission_failures{};
unsigned CategoryIndex(std::string_view name) noexcept {
    for (unsigned index = 0; index < category_names.size(); ++index) {
        if (name == category_names[index]) { return index; }
    }
    return static_cast<unsigned>(category_names.size() - 1);
}
std::uint64_t DurationUs(Clock::duration duration) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
}
std::size_t FormatFrame(char* buffer, std::size_t capacity, const Counters& values,
        std::uint64_t submission, std::uint64_t clock, std::uint64_t interval,
        std::uint64_t previous, std::uint64_t previous_submission,
        std::uint64_t active, bool previous_active, unsigned thread, bool full_allocation_timing = true) noexcept {
    std::size_t used{};
    auto append = [&](const char* format, auto... args) {
        if (used >= capacity) { return; }
        const auto size = std::snprintf(buffer + used, capacity - used, format, args...);
        if (size < 0 || static_cast<std::size_t>(size) >= capacity - used) { used = capacity; }
        else { used += static_cast<std::size_t>(size); }
    };
    // floor(end) - floor(begin) can exceed floor(end - begin) by one us.
    // Submission adjacency, not a loose clock tolerance, establishes that this
    // thread actually observed the preceding global completion boundary.
    const bool aligned = submission && previous_submission == submission - 1 && previous && clock >= previous &&
        clock - previous >= interval && clock - previous - interval <= 1;
    append("{\"event\":\"native_host_frame_timing\",\"submission\":%llu,\"clock_us\":%llu,\"interval_us\":%llu,\"counters_began_clock_us\":%llu,\"counters_began_submission\":%llu,\"thread\":%u,\"same_thread_interval\":%s,\"active_scopes_at_boundary\":%llu,\"cross_boundary_completed_scopes\":%llu,\"bounded_to_interval\":%s,\"outer_calls\":%llu,\"outer_us\":%llu,\"allocation_timing\":\"%s\",\"outer_coverage\":\"timed_scopes_only\",\"categories\":{",
        submission, clock, interval, previous, previous_submission, thread, aligned ? "true" : "false", active, values.cross_boundary,
        aligned && !active && !previous_active && !values.cross_boundary ? "true" : "false", values.outer.calls, DurationUs(values.outer.total),
        full_allocation_timing ? "full" : "counts");
    for (unsigned index = 0; index < category_names.size(); ++index) {
        const auto value = values.Category(index);
        if (!full_allocation_timing && index == 0) {
            append("%s\"%s\":{\"calls\":%llu,\"total_us\":null,\"max_us\":null}", index ? "," : "", category_names[index], value.calls);
        } else {
            append("%s\"%s\":{\"calls\":%llu,\"total_us\":%llu,\"max_us\":%llu}", index ? "," : "", category_names[index],
                value.calls, DurationUs(value.total), DurationUs(value.maximum));
        }
    }
    append("%s", "},\"allocation_operations\":{");
    for (unsigned index = 0; index < allocation_names.size(); ++index) {
        const auto& value = values.allocations[index];
        if (full_allocation_timing) {
            append("%s\"%s\":{\"calls\":%llu,\"total_us\":%llu,\"max_us\":%llu}", index ? "," : "", allocation_names[index],
                value.calls, DurationUs(value.total), DurationUs(value.maximum));
        } else {
            append("%s\"%s\":{\"calls\":%llu,\"total_us\":null,\"max_us\":null}", index ? "," : "", allocation_names[index], value.calls);
        }
    }
    append("%s", "}}\n");
    return used < capacity ? used : 0;
}
}

NativeHostOperationScope::NativeHostOperationScope(const char* name, const char* category,
        std::uint64_t detail, const void* caller, bool enabled, NativeHostAllocationKind allocation) noexcept
    : name_(name), category_(category), detail_(detail), caller_(caller), enabled_(enabled), allocation_(allocation) {
    if (enabled_) {
        const auto error = GetLastError(); const auto host_errno = errno;
        category_index_ = CategoryIndex(category_);
        counts_only_ = !allocation_timing.AcquireFull() && category_index_ == 0;
        ++frame_counters.depth;
        began_submission_ = frame_counters.previous_submission;
        if (!counts_only_) {
            outer_ = frame_counters.timed_depth++ == 0;
            began_ = std::chrono::steady_clock::now();
        }
        errno = host_errno; SetLastError(error);
    }
}
NativeHostOperationScope::~NativeHostOperationScope() noexcept {
    if (!enabled_) { return; }
    const auto error = GetLastError(); const auto host_errno = errno;
    --frame_counters.depth;
    if (began_submission_ != frame_counters.previous_submission && frame_counters.values.cross_boundary != UINT64_MAX) {
        ++frame_counters.values.cross_boundary;
    }
    if (counts_only_) {
        frame_counters.values.Add(0, Clock::duration::zero(), false, allocation_);
        errno = host_errno; SetLastError(error); return;
    }
    const auto finished = std::chrono::steady_clock::now();
    const auto completed = Microseconds(finished), began = Microseconds(began_);
    --frame_counters.timed_depth;
    if (finished >= began_) {
        // Sum native clock durations before conversion; many sub-microsecond
        // calls must not disappear through per-call integer truncation.
        frame_counters.values.Add(category_index_, finished - began_, outer_, allocation_);
    }
    if (completed < began || completed - began < kThresholdUs) {
        errno = host_errno; SetLastError(error); return;
    }
    const std::string_view category(category_);
    const bool routine_wait = completed >= began && completed - began < 25000 &&
        (category == "synchronization" || category == "sleep");
    auto& selected = routine_wait ? routine_wait_budget : budget;
    auto& local = routine_wait ? routine_wait_thread_budget : thread_budget;
    if (selected.Admit(began, completed, local)) {
        const auto suppressed = budget.pending.exchange(0, std::memory_order_relaxed) +
                                routine_wait_budget.pending.exchange(0, std::memory_order_relaxed);
        if (!EmitNativeHostOperation(name_, category_, detail_, caller_, began, completed, suppressed)) {
            selected.failures.fetch_add(1, std::memory_order_relaxed);
        }
    }
    errno = host_errno; SetLastError(error);
}
bool ConfigureNativeAllocationTiming(bool full) noexcept {
    const auto error = GetLastError(); const auto host_errno = errno;
    const bool configured = allocation_timing.Configure(full);
    if (configured) { std::printf("{\"event\":\"native_host_timing_configuration\",\"allocation_timing\":\"%s\",\"startup_only\":true}\n", full ? "full" : "counts"); }
    errno = host_errno; SetLastError(error); return configured;
}
void RecordNativeHostFrameTiming(std::uint64_t submission, std::uint64_t clock_us, std::uint64_t interval_us) noexcept {
    const auto error = GetLastError(); const auto host_errno = errno;
    const auto values = frame_counters.values;
    frame_counters.values = {};
    const auto previous = frame_counters.previous_clock;
    const auto previous_submission = frame_counters.previous_submission;
    const auto previous_active = frame_counters.previous_active;
    frame_counters.previous_clock = clock_us;
    frame_counters.previous_submission = submission;
    frame_counters.previous_active = frame_counters.depth != 0;
    if (interval_us >= 25000) {
        char buffer[4096];
        const auto size = FormatFrame(buffer, sizeof(buffer), values, submission, clock_us, interval_us,
            previous, previous_submission, frame_counters.depth, previous_active, GetCurrentThreadId(), allocation_timing.Full());
        // Construct the whole line before touching stdout. One fwrite keeps
        // other threads from interleaving fragments of the JSON object.
        if (!size || std::fwrite(buffer, 1, size, stdout) != size) { frame_emission_failures.fetch_add(1, std::memory_order_relaxed); }
    }
    errno = host_errno; SetLastError(error);
}
void PrintNativeHostTimingDiagnostics() noexcept {
    const auto error = GetLastError(); const auto host_errno = errno;
    std::printf("{\"event\":\"native_host_timing_diagnostics\",\"threshold_us\":%llu,\"work_thread_second_limit\":8,\"work_process_second_limit\":48,\"routine_wait_thread_second_limit\":2,\"routine_wait_process_second_limit\":16,\"long_wait_us\":25000,\"admitted\":%llu,\"emission_failures\":%llu,\"suppressed\":%llu,\"suppressed_not_yet_reported\":%llu,\"frame_emission_failures\":%llu,\"allocation_timing\":\"%s\",\"snapshot_only\":true}\n",
        kThresholdUs, budget.emitted.load() + routine_wait_budget.emitted.load(),
        budget.failures.load() + routine_wait_budget.failures.load(),
        budget.suppressed.load() + routine_wait_budget.suppressed.load(),
        budget.pending.load() + routine_wait_budget.pending.load(), frame_emission_failures.load(), allocation_timing.Full() ? "full" : "counts");
    errno = host_errno; SetLastError(error);
}

bool NativeHostTimingSelfTest() {
    bool passed = ConfigureNativeAllocationTiming(true); unsigned cases{};
    const auto check = [&](const char* name, bool value) {
        ++cases; passed &= value;
        std::printf("{\"event\":\"host_timing_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    {
        Budget item; ThreadBudget local;
        check("short_and_reversed_times_are_not_slow_records", !item.Admit(10, 8009, local) && !item.Admit(20, 19, local) && !item.emitted && !item.suppressed);
        check("threshold_is_inclusive", item.Admit(10, 8010, local) && item.emitted == 1);
    }
    {
        Budget item; ThreadBudget local; unsigned accepted{};
        for (unsigned i = 0; i < 20; ++i) { accepted += item.Admit(0, 9000, local); }
        check("per_thread_window_is_bounded", accepted == 8 && item.suppressed == 12 && item.pending == 12);
        check("next_second_resets_thread_and_global_windows", item.Admit(1000000, 1009000, local));
        check("clock_rollback_cannot_reopen_an_old_window", !item.Admit(0, 9000, local));
    }
    {
        Budget item; std::array<ThreadBudget, 16> locals{}; unsigned accepted{};
        for (auto& local : locals) { for (unsigned i = 0; i < 8; ++i) { accepted += item.Admit(0, 9000, local); } }
        check("process_limit_applies_across_threads", accepted == 64 && item.suppressed == 64);
        ThreadBudget later; check("new_global_window_is_available", item.Admit(2000000, 2009000, later));
        ThreadBudget stale; check("fresh_thread_cannot_roll_global_window_back", !item.Admit(0, 9000, stale));
        check("pending_suppression_is_consumed_once", item.pending.exchange(0) == 65 && item.pending.exchange(0) == 0 && item.suppressed == 65);
    }
    {
        Budget item; std::vector<std::thread> workers;
        for (unsigned i = 0; i < 16; ++i) { workers.emplace_back([&] { ThreadBudget local; for (unsigned j = 0; j < 32; ++j) { (void)item.Admit(0, 9000, local); } }); }
        for (auto& worker : workers) { worker.join(); }
        check("concurrent_admission_never_exceeds_process_limit", item.emitted == 64 && item.suppressed == 448 && item.pending == 448);
    }
    {
        const auto before = budget.emitted.load();
        { NativeHostOperationScope scope("authored.disabled", "fixture", 0, nullptr, false); Sleep(10); }
        check("disabled_scope_does_not_record", budget.emitted == before);
        { NativeHostOperationScope scope("authored.wait", "fixture", 12000, nullptr); Sleep(12); errno = ERANGE; SetLastError(ERROR_TIMEOUT); }
        const auto error = GetLastError(); const auto host_errno = errno;
        check("slow_scope_preserves_completed_operation_error_state", error == ERROR_TIMEOUT && host_errno == ERANGE && budget.emitted > before);
    }
    {
        const auto before = budget.emitted.load(); bool caught{};
        try { NativeHostOperationScope scope("authored.exception", "fixture", 0, nullptr); Sleep(12); throw std::runtime_error("authored timing unwind"); }
        catch (const std::runtime_error&) { caught = true; }
        check("host_unwind_remains_safe_and_records_a_completed_scope", caught && budget.emitted > before);
    }
    {
        Counters values;
        for (unsigned index = 0; index < 1000; ++index) { values.Add(1, Clock::duration(1), false); }
        check("short_calls_accumulate_clock_ticks_before_microsecond_conversion", values.categories[1].calls == 1000 &&
            values.categories[1].total == Clock::duration(1000) && DurationUs(values.categories[1].total) == DurationUs(Clock::duration(1000)));
        values = {};
        values.Add(0, std::chrono::microseconds(12500), true);
        values.Add(1, std::chrono::microseconds(3000), false);
        values.Add(2, std::chrono::microseconds(1500), false);
        check("nested_categories_do_not_double_count_outer_coverage", values.outer.calls == 1 && DurationUs(values.outer.total) == 12500 &&
            DurationUs(values.categories[1].total) == 3000 && DurationUs(values.categories[2].total) == 1500);
        char buffer[4096];
        auto size = FormatFrame(buffer, sizeof(buffer), values, 7, 150000, 50000, 100000, 6, 0, false, 73);
        auto text = std::string_view(buffer, size);
        check("bounded_frame_json_is_complete_and_has_exact_outer_time", size && text.ends_with("}}\n") &&
            text.find("\"bounded_to_interval\":true") != text.npos && text.find("\"outer_us\":12500") != text.npos);
        bool unbounded = true;
        for (unsigned mode = 0; mode < 4; ++mode) {
            values.cross_boundary = mode == 3 ? 1 : 0;
            size = FormatFrame(buffer, sizeof(buffer), values, 7, 150000, 50000, mode == 0 ? 90000 : 100000,
                6, mode == 1 ? 1 : 0, mode == 2, 73);
            unbounded &= size && std::string_view(buffer, size).find("\"bounded_to_interval\":false") != std::string_view::npos;
        }
        check("thread_switch_and_cross_boundary_scopes_are_not_called_bounded", unbounded);
        values.cross_boundary = 0;
        bool rounding_correct = true;
        for (unsigned mode = 0; mode < 6; ++mode) {
            const auto previous = mode == 0 ? 99999ull : mode == 1 ? 100001ull : mode == 2 ? 99998ull : 100000ull;
            const auto previous_submission = mode == 3 ? 5ull : mode == 4 ? 7ull : 6ull;
            const auto submission = mode == 5 ? 0ull : 7ull;
            size = FormatFrame(buffer, sizeof(buffer), values, submission, 150000, 50000, previous,
                previous_submission, 0, false, 73);
            rounding_correct &= size && std::string_view(buffer, size).find(mode == 0 ?
                "\"bounded_to_interval\":true" : "\"bounded_to_interval\":false") != std::string_view::npos;
        }
        check("clock_rounding_accepts_only_one_us_and_consecutive_submission", rounding_correct);
        values = {}; values.categories[1].calls = UINT64_MAX;
        values.categories[1].total = Clock::duration::max() - Clock::duration(2);
        values.Add(1, Clock::duration(10), false);
        check("counter_saturation_and_small_output_buffer_do_not_overflow", values.categories[1].calls == UINT64_MAX &&
            values.categories[1].total == Clock::duration::max() && !FormatFrame(buffer, 32, values, 7, 150000, 50000, 100000, 6, 0, false, 73));
        values = {};
        values.Add(0, Clock::duration(1501), true, NativeHostAllocationKind::Malloc);
        values.Add(0, Clock::duration(1501), true, NativeHostAllocationKind::FreeDelete);
        values.Add(0, Clock::duration(1), true, static_cast<NativeHostAllocationKind>(99));
        const auto total = values.Category(0);
        check("allocation_kind_totals_accumulate_raw_ticks_without_double_counting", total.calls == 3 &&
            total.total == Clock::duration(3003) && total.maximum == Clock::duration(1501) &&
            values.allocations[3].calls == 1 && values.allocations[5].calls == 1 && values.allocations[0].calls == 1 &&
            values.outer.calls == 3 && values.outer.total == total.total);
        values.Add(1, Clock::duration(5), false, NativeHostAllocationKind::Malloc);
        check("allocation_kind_does_not_reclassify_other_categories", values.categories[1].calls == 1 && values.Category(0).calls == 3);
        for (auto& value : values.categories) { value = Counter{UINT64_MAX, Clock::duration::max(), Clock::duration::max()}; }
        for (auto& value : values.allocations) { value = Counter{UINT64_MAX, Clock::duration::max(), Clock::duration::max()}; }
        size = FormatFrame(buffer, sizeof(buffer), values, UINT64_MAX, UINT64_MAX, 50000, UINT64_MAX - 50000, UINT64_MAX - 1, UINT64_MAX, true, (std::numeric_limits<unsigned>::max)());
        check("all_allocation_kinds_fit_bounded_output_even_at_saturated_counters", size && size < sizeof(buffer) &&
            std::string_view(buffer, size).find("\"mspace_free\":{") != std::string_view::npos && values.Category(0).calls == UINT64_MAX);
    }
    {
        const auto before = frame_counters.values.categories[5].calls;
        bool isolated{};
        std::thread worker([&] {
            isolated = frame_counters.values.categories[5].calls == 0;
            frame_counters.values.Add(5, std::chrono::microseconds(1), true);
        }); worker.join();
        check("completed_operation_counters_are_thread_local", isolated && frame_counters.values.categories[5].calls == before);
        errno = EDOM; SetLastError(ERROR_TIMEOUT);
        RecordNativeHostFrameTiming(100, 1000000, 0);
        const auto error = GetLastError(); const auto host_errno = errno;
        check("every_frame_resets_counters_without_changing_error_state", !frame_counters.values.outer.calls &&
            !frame_counters.values.categories[5].calls && frame_counters.previous_clock == 1000000 &&
            frame_counters.previous_submission == 100 && error == ERROR_TIMEOUT && host_errno == EDOM);
        frame_counters.values.Add(1, std::chrono::microseconds(5000), true);
        frame_counters.values.Add(3, std::chrono::microseconds(10000), true);
        frame_counters.values.Add(0, std::chrono::microseconds(1200), true, NativeHostAllocationKind::Malloc);
        frame_counters.values.Add(0, std::chrono::microseconds(800), true, NativeHostAllocationKind::FreeDelete);
        RecordNativeHostFrameTiming(101, 1030000, 30000);
        check("long_frame_emits_then_clears_only_its_thread_snapshot", !frame_counters.values.outer.calls && frame_counters.previous_clock == 1030000);
    }
    PrintNativeHostTimingDiagnostics();
    std::printf("{\"event\":\"host_timing_self_test\",\"cases\":%u,\"passed\":%s}\n", cases, passed ? "true" : "false");
    return passed;
}

bool NativeHostCountsOnlySelfTest(bool configure) {
    bool passed = true; unsigned cases{};
    const auto check = [&](const char* name, bool value) {
        ++cases; passed &= value;
        std::printf("{\"event\":\"host_counts_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    check(configure ? "counts_mode_configured_before_any_scope" : "counts_mode_is_default",
        configure ? ConfigureNativeAllocationTiming(false) : !allocation_timing.Full());
    RecordNativeHostFrameTiming(100, 1000000, 0);
    errno = EDOM; SetLastError(ERROR_TIMEOUT);
    bool constructor_preserved{};
    {
        NativeHostOperationScope scope("authored.counted_malloc", "allocation", 32, nullptr, true, NativeHostAllocationKind::Malloc);
        constructor_preserved = errno == EDOM && GetLastError() == ERROR_TIMEOUT;
        Sleep(10); // Deliberately long but not a duration sample in counts mode.
        errno = EILSEQ; SetLastError(ERROR_INVALID_PARAMETER);
    }
    const auto host_errno = errno; const auto error = GetLastError();
    check("counts_scope_preserves_constructor_and_body_error_states", constructor_preserved && host_errno == EILSEQ && error == ERROR_INVALID_PARAMETER);
    {
        NativeHostOperationScope allocation("authored.counted_free", "allocation", 0, nullptr, true, NativeHostAllocationKind::FreeDelete);
        NativeHostOperationScope timed("authored.measured_inside_counted_scope", "fixture", 0, nullptr);
        Sleep(1);
    }
    {
        NativeHostOperationScope disabled("authored.disabled", "allocation", 32, nullptr, false, NativeHostAllocationKind::New);
    }
    const auto allocation = frame_counters.values.Category(0);
    check("counts_are_exact_without_allocating_a_duration_sample", allocation.calls == 2 && allocation.total == Clock::duration::zero() &&
        frame_counters.values.allocations[3].calls == 1 && frame_counters.values.allocations[5].calls == 1 && !frame_counters.values.allocations[1].calls);
    check("timed_nested_scope_remains_outer_measured_coverage", frame_counters.values.outer.calls == 1 &&
        frame_counters.values.outer.total > Clock::duration::zero() && !frame_counters.depth && !frame_counters.timed_depth);
    bool isolated{};
    std::thread worker([&] {
        const bool empty = !frame_counters.values.Category(0).calls;
        { NativeHostOperationScope scope("authored.worker", "allocation", 16, nullptr, true, NativeHostAllocationKind::New); }
        isolated = empty && frame_counters.values.allocations[1].calls == 1;
    }); worker.join();
    check("counts_mode_is_thread_local_and_disabled_scopes_are_ignored", isolated && !frame_counters.values.allocations[1].calls);
    check("mode_cannot_change_after_first_enabled_scope", !ConfigureNativeAllocationTiming(true) && !allocation_timing.Full());
    errno = EDOM; SetLastError(ERROR_TIMEOUT);
    RecordNativeHostFrameTiming(101, 1030000, 30000);
    const auto reset_errno = errno; const auto reset_error = GetLastError();
    check("counts_frame_emission_resets_and_preserves_errors", !frame_counters.values.Category(0).calls && reset_errno == EDOM && reset_error == ERROR_TIMEOUT);
    {
        NativeHostOperationScope across("authored.across_frame", "allocation", 16, nullptr, true, NativeHostAllocationKind::New);
        RecordNativeHostFrameTiming(200, 2000000, 0);
    }
    check("counts_only_cross_frame_scope_is_not_called_bounded", frame_counters.values.cross_boundary == 1 &&
        frame_counters.previous_active && !frame_counters.depth && frame_counters.values.allocations[1].calls == 1);
    RecordNativeHostFrameTiming(201, 2030000, 30000);
    PrintNativeHostTimingDiagnostics();
    std::printf("{\"event\":\"host_counts_self_test\",\"cases\":%u,\"passed\":%s}\n", cases, passed ? "true" : "false");
    return passed;
}
