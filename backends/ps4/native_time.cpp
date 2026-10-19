#include "native_time.h"
#include "native_diagnostics.h"
#include "native_host_timing.h"

#include <windows.h>
#include <intrin.h>

#include <cstdio>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <algorithm>
#include <array>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
struct Clock {
    std::uint64_t frequency, start, tsc_start;
    Clock() {
        LARGE_INTEGER frequency_value{}, start_value{};
        if (!QueryPerformanceFrequency(&frequency_value) || frequency_value.QuadPart <= 0 ||
            static_cast<std::uint64_t>(frequency_value.QuadPart) >
                (std::numeric_limits<std::uint64_t>::max)() / 1000000 ||
            !QueryPerformanceCounter(&start_value) || start_value.QuadPart < 0) {
            throw std::runtime_error("Cannot initialize the monotonic process clock");
        }
        frequency = static_cast<std::uint64_t>(frequency_value.QuadPart);
        start = static_cast<std::uint64_t>(start_value.QuadPart);
        tsc_start = NativeHostTsc();
    }
};
Clock& ProcessClock() { static Clock clock; return clock; }

std::uint64_t QpcNow() {
    LARGE_INTEGER value{};
    if (!QueryPerformanceCounter(&value) || value.QuadPart < 0) {
        UnsupportedApi("native_counter_qpc_failure", GetLastError(), __builtin_return_address(0));
    }
    return static_cast<std::uint64_t>(value.QuadPart);
}
struct TscPoint { std::uint64_t tsc, qpc, bracket; };
TscPoint MeasureTscPoint() {
    const auto before = QpcNow();
    const auto tsc = NativeHostTsc();
    const auto after = QpcNow();
    if (after < before) { UnsupportedApi("native_counter_qpc_reversed", 0, __builtin_return_address(0)); }
    return {tsc, before + (after - before) / 2, after - before};
}
std::uint64_t ScaleCounter(std::uint64_t value, std::uint64_t numerator, std::uint64_t denominator) {
    if (!denominator) { UnsupportedApi("native_counter_zero_frequency", 0, __builtin_return_address(0)); }
    const auto result = static_cast<__uint128_t>(value) * numerator / denominator;
    if (result > UINT64_MAX) { UnsupportedApi("native_counter_scale_overflow", 0, __builtin_return_address(0)); }
    return static_cast<std::uint64_t>(result);
}
struct TscClock {
    std::uint64_t frequency{}, spread_ppm{};
    TscClock() {
        int registers[4]{};
        __cpuid(registers, static_cast<int>(0x80000000u));
        if (static_cast<unsigned>(registers[0]) < 0x80000007u) {
            UnsupportedApi("native_invariant_tsc_unavailable", 0, nullptr);
        }
        __cpuid(registers, static_cast<int>(0x80000007u));
        if (!(static_cast<unsigned>(registers[3]) & (1u << 8))) {
            UnsupportedApi("native_invariant_tsc_unavailable", 0, nullptr);
        }
        const auto qpc_frequency = ProcessClock().frequency;
        std::array<std::uint64_t, 3> samples{};
        unsigned accepted = 0;
        for (unsigned attempt = 0; attempt < 9 && accepted < samples.size(); ++attempt) {
            const auto first = MeasureTscPoint();
            Sleep(16); // Measure actual elapsed QPC time, not the requested sleep.
            const auto last = MeasureTscPoint();
            if (last.tsc <= first.tsc || last.qpc <= first.qpc ||
                first.bracket > qpc_frequency / 2000 || last.bracket > qpc_frequency / 2000) { continue; }
            const auto estimate = ScaleCounter(last.tsc - first.tsc, qpc_frequency, last.qpc - first.qpc);
            if (estimate < 1000000 || estimate > 100000000000ull) { continue; }
            samples[accepted++] = estimate;
        }
        if (accepted != samples.size()) { UnsupportedApi("native_tsc_calibration_failed", accepted, nullptr); }
        std::sort(samples.begin(), samples.end()); frequency = samples[1];
        spread_ppm = ScaleCounter(samples[2] - samples[0], 1000000, frequency);
        if (spread_ppm > 10000) { UnsupportedApi("native_tsc_calibration_unstable", spread_ppm, nullptr); }
        std::printf("{\"event\":\"native_counter_clock\",\"domain\":\"host_invariant_tsc\","
                    "\"frequency_hz\":%llu,\"qpc_frequency_hz\":%llu,\"sample_spread_ppm\":%llu,"
                    "\"frequency_method\":\"qpc_calibrated_median3\",\"frequency_is_estimate\":true}\n",
                    frequency, qpc_frequency, spread_ppm);
    }
};
const TscClock& ProcessTscClock() { static TscClock clock; return clock; }
std::uint64_t KT_SYSV ProcessCounter() {
    const auto& process = ProcessClock(); (void)ProcessTscClock();
    const auto current = NativeHostTsc();
    if (current < process.tsc_start) { UnsupportedApi("native_process_tsc_reversed", 0, __builtin_return_address(0)); }
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) { TraceNativeApi("sceKernelGetProcessTimeCounter", current - process.tsc_start, __builtin_return_address(0)); }
    return current - process.tsc_start;
}
std::uint64_t KT_SYSV CounterFrequency() { return ProcessTscClock().frequency; }
std::uint64_t KT_SYSV ReadTsc() { (void)ProcessTscClock(); return NativeHostTsc(); }

std::uint64_t ReadMicroseconds() {
    const auto& clock = ProcessClock();
    LARGE_INTEGER current{};
    if (!QueryPerformanceCounter(&current) || current.QuadPart < 0 ||
        static_cast<std::uint64_t>(current.QuadPart) < clock.start) {
        UnsupportedApi("native_monotonic_clock_failure", GetLastError(), __builtin_return_address(0));
    }
    const auto delta = static_cast<std::uint64_t>(current.QuadPart) - clock.start;
    return (delta / clock.frequency) * 1000000 + (delta % clock.frequency) * 1000000 / clock.frequency;
}
std::uint64_t KT_SYSV ProcessMicroseconds() {
    const auto microseconds = ReadMicroseconds();
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) {
        TraceNativeApi("sceKernelGetProcessTime", microseconds, __builtin_return_address(0));
    }
    return microseconds;
}

int KT_SYSV MicrosecondSleep(unsigned duration) {
    NativeHostOperationScope timing("sceKernelUsleep", "sleep", duration, __builtin_return_address(0));
    if (!duration) { Sleep(0); return 0; }
    auto timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!timer && GetLastError() == ERROR_INVALID_PARAMETER) {
        timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
    }
    if (!timer) { return static_cast<int>(0x8002000cu); }
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(duration) * 10; // relative 100 ns units
    int error = 0;
    if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) || WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0) {
        error = static_cast<int>(0x80020016u);
    }
    CloseHandle(timer);
    return error;
}
} // namespace

void InitializeNativeTime() { (void)ProcessClock(); }
std::uint64_t NativeProcessMicroseconds() { return ReadMicroseconds(); }
std::uint64_t NativeHostTsc() {
    // ABI timestamp only, not the scheduling clock and not QPC ticks. A guest
    // TSC frequency contract is still separate from this raw host counter.
    _mm_lfence();
    const auto value = __rdtsc();
    _mm_lfence();
    return value;
}

std::uint64_t ResolveTimeImport(std::string_view identifier) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "4J2sUJmuHZQ") { return reinterpret_cast<std::uint64_t>(&ProcessMicroseconds); }
    if (hash == "1jfXLRVzisc") { return reinterpret_cast<std::uint64_t>(&MicrosecondSleep); }
    if (hash == "fgxnMeTNUtY") { return reinterpret_cast<std::uint64_t>(&ProcessCounter); }
    if (hash == "BNowx2l588E" || hash == "1j3S3n-tTW4") { return reinterpret_cast<std::uint64_t>(&CounterFrequency); }
    if (hash == "-2IRUCO--PM") { return reinterpret_cast<std::uint64_t>(&ReadTsc); }
    return 0;
}

bool TimeSelfTest() {
    InitializeNativeTime();
    const auto start = ProcessMicroseconds();
    bool monotonic = true;
    auto previous = start;
    for (unsigned i = 0; i < 10000; ++i) {
        const auto current = ProcessMicroseconds();
        monotonic &= current >= previous;
        previous = current;
    }
    const auto before = ProcessMicroseconds();
    const auto error = MicrosecondSleep(20000);
    const auto elapsed = ProcessMicroseconds() - before;
    const auto frequency = CounterFrequency();
    const auto counter_start = ProcessCounter(), tsc_start = ReadTsc();
    const auto qpc_start = QpcNow();
    const auto slept = MicrosecondSleep(30000);
    const auto qpc_end = QpcNow();
    const auto counter_end = ProcessCounter(), tsc_end = ReadTsc();
    const auto qpc_us = ScaleCounter(qpc_end - qpc_start, 1000000, ProcessClock().frequency);
    const auto counter_us = ScaleCounter(counter_end - counter_start, 1000000, frequency);
    const auto tsc_us = ScaleCounter(tsc_end - tsc_start, 1000000, frequency);
    const auto difference = [](std::uint64_t a, std::uint64_t b) { return a > b ? a - b : b - a; };
    const bool counter_valid = !slept && frequency > 1000000 && counter_end > counter_start && tsc_end > tsc_start &&
        qpc_us >= 30000 && difference(counter_us, qpc_us) < qpc_us / 50 + 1000 && difference(tsc_us, qpc_us) < qpc_us / 50 + 1000 &&
        tsc_start >= counter_start && tsc_end >= counter_end &&
        ScaleCounter((1ull << 48) + 19, 7, 7) == (1ull << 48) + 19 &&
        ScaleCounter(UINT64_MAX, UINT64_MAX, UINT64_MAX) == UINT64_MAX;
    const bool passed = monotonic && !error && elapsed >= 20000 && MicrosecondSleep(0) == 0 && counter_valid;
    std::printf("{\"event\":\"time_self_test\",\"monotonic\":%s,\"requested_us\":20000,"
                "\"elapsed_us\":%llu,\"counter_frequency_hz\":%llu,\"counter_us\":%llu,\"raw_tsc_us\":%llu,"
                "\"qpc_us\":%llu,\"counter_valid\":%s,\"passed\":%s}\n", monotonic ? "true" : "false", elapsed,
                frequency, counter_us, tsc_us, qpc_us, counter_valid ? "true" : "false", passed ? "true" : "false");
    return passed;
}
