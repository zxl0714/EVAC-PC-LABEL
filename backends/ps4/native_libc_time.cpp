#include "native_libc_time.h"
#include "native_memory.h"
#include "native_rtc.h"
#include "native_diagnostics.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
// Verified against the original libc calendar writer at +0x14bb0 and both
// main-program mktime callers: nine signed 32-bit fields, no FreeBSD tail.
struct GuestTm { std::int32_t sec, min, hour, mday, mon, year, wday, yday, isdst; };
static_assert(sizeof(GuestTm) == 36 && offsetof(GuestTm, year) == 20 && offsetof(GuestTm, isdst) == 32);
constexpr std::int64_t kMinimum = -62135596800ll, kMaximum = 253402300799ll;
thread_local GuestTm calendar_storage{}; // gmtime/localtime share one per-thread result, as the original libc does.
template<class T> bool Read(const T* input, T& output) {
    SIZE_T bytes{};
    return input && ReadProcessMemory(GetCurrentProcess(), input, &output, sizeof(T), &bytes) && bytes == sizeof(T);
}
bool Writable(const void* address, std::size_t bytes) {
    return address && NativeMemoryWritable(address, bytes);
}
bool Calendar(std::int64_t seconds, int daylight, GuestTm& output) {
    if (seconds < kMinimum || seconds > kMaximum) { return false; }
    auto days = seconds / 86400, within = seconds % 86400;
    if (within < 0) { --days; within += 86400; }
    const std::chrono::sys_days day{std::chrono::days{static_cast<int>(days)}};
    const std::chrono::year_month_day date{day};
    const auto first = std::chrono::sys_days{date.year()/1/1};
    output = {static_cast<int>(within % 60), static_cast<int>(within / 60 % 60), static_cast<int>(within / 3600),
        static_cast<int>(unsigned(date.day())), static_cast<int>(unsigned(date.month())) - 1, int(date.year()) - 1900,
        static_cast<int>(std::chrono::weekday{day}.c_encoding()), static_cast<int>((day - first).count()), daylight};
    return true;
}
bool Normalize(const GuestTm& input, std::int64_t& output) {
    const auto month = static_cast<std::int64_t>(input.mon);
    auto year_delta = month / 12;
    if (month % 12 < 0) { --year_delta; }
    const auto year = static_cast<std::int64_t>(input.year) + 1900 + year_delta;
    if (year < 1 || year > 9999) { return false; }
    const auto normalized_month = static_cast<unsigned>(month - year_delta * 12 + 1);
    const auto first = std::chrono::sys_days{std::chrono::year{static_cast<int>(year)}/std::chrono::month{normalized_month}/1};
    const auto seconds = static_cast<__int128_t>(first.time_since_epoch().count()) * 86400 +
        (static_cast<__int128_t>(input.mday) - 1) * 86400 + static_cast<__int128_t>(input.hour) * 3600 +
        static_cast<__int128_t>(input.min) * 60 + input.sec;
    if (seconds < kMinimum || seconds > kMaximum) { return false; }
    output = static_cast<std::int64_t>(seconds); return true;
}
std::int64_t KT_SYSV Time(std::int64_t* output) {
    if (output && !Writable(output, sizeof(*output))) { SetNativeErrno(14); return -1; }
    std::int64_t seconds{};
    if (!NativeUtcClockSeconds(seconds)) { SetNativeErrno(84); return -1; }
    if (output) { std::memcpy(output, &seconds, sizeof(seconds)); }
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) { TraceNativeApi("time", static_cast<std::uint64_t>(seconds), __builtin_return_address(0)); }
    return seconds;
}
GuestTm* KT_SYSV Gmtime(const std::int64_t* input) {
    std::int64_t seconds{}; GuestTm value{};
    if (!Read(input, seconds)) { SetNativeErrno(14); return nullptr; }
    if (!Calendar(seconds, 0, value)) { SetNativeErrno(84); return nullptr; }
    calendar_storage = value;
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) { TraceNativeApi("gmtime", static_cast<std::uint64_t>(seconds), __builtin_return_address(0)); }
    return &calendar_storage;
}
GuestTm* KT_SYSV Localtime(const std::int64_t* input) {
    std::int64_t seconds{}, local{}; int daylight{}; GuestTm value{};
    if (!Read(input, seconds)) { SetNativeErrno(14); return nullptr; }
    if (!NativeUtcToLocalSeconds(seconds, local, daylight) || !Calendar(local, daylight, value)) { SetNativeErrno(84); return nullptr; }
    calendar_storage = value;
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) { TraceNativeApi("localtime", static_cast<std::uint64_t>(seconds), __builtin_return_address(0)); }
    return &calendar_storage;
}
std::int64_t KT_SYSV Mktime(GuestTm* input) {
    GuestTm value{}, normalized{}; std::int64_t local{}, utc{}, normalized_local{}; int daylight{};
    if (!Read(input, value) || !Writable(input, sizeof(*input))) { SetNativeErrno(14); return -1; }
    if (!Normalize(value, local) || !NativeLocalToUtcSeconds(local, value.isdst, utc, normalized_local, daylight) ||
        !Calendar(normalized_local, daylight, normalized)) { SetNativeErrno(84); return -1; }
    std::memcpy(input, &normalized, sizeof(normalized));
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) { TraceNativeApi("mktime", static_cast<std::uint64_t>(utc), __builtin_return_address(0)); }
    return utc;
}
}

std::uint64_t ResolveLibcCalendarImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libc" || module != "libc") { return 0; }
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "wLlFkwG9UcQ") { return reinterpret_cast<std::uint64_t>(&Time); }
    if (hash == "efhK-YSUYYQ") { return reinterpret_cast<std::uint64_t>(&Localtime); }
    if (hash == "1mecP7RgI2A") { return reinterpret_cast<std::uint64_t>(&Gmtime); }
    if (hash == "n7AepwR0s34") { return reinterpret_cast<std::uint64_t>(&Mktime); }
    return 0;
}

bool LibcCalendarSelfTest() {
    bool okay = true; unsigned cases{};
    const auto check = [&](bool passed, const char* name) {
        ++cases; okay &= passed;
        std::printf("{\"event\":\"libc_calendar_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, passed ? "true" : "false");
    };
    for (const auto seconds : std::array<std::int64_t, 6>{0, -1, 2147483648ll, 2208988800ll, kMinimum, kMaximum}) {
        GuestTm value{}; std::int64_t back{};
        check(Calendar(seconds, 0, value) && Normalize(value, back) && back == seconds, "utc_calendar_roundtrip");
    }
    std::int64_t epoch{};
    auto* first = Gmtime(&epoch);
    const auto before = calendar_storage;
    auto invalid = (std::numeric_limits<std::int64_t>::max)();
    check(first && !Gmtime(&invalid) && std::memcmp(&before, &calendar_storage, sizeof(before)) == 0,
          "invalid_time_preserves_shared_result");
    auto* second = Localtime(&epoch);
    check(first && first == second, "gmtime_localtime_share_thread_result");
    std::int64_t wide = 2208988800ll;
    auto* local = Localtime(&wide);
    auto copy = local ? *local : GuestTm{};
    check(local && Mktime(&copy) == wide, "local_calendar_64bit_roundtrip");
    GuestTm noncanonical{90,59,23,0,0,126,73,73,-1};
    check(Mktime(&noncanonical) != -1 && noncanonical.year == 126 && noncanonical.mon == 0 && noncanonical.mday == 1 &&
        noncanonical.hour == 0 && noncanonical.min == 0 && noncanonical.sec == 30, "mktime_normalizes_fields");
    GuestTm excessive{}; excessive.year = INT_MAX;
    const auto saved = excessive;
    check(Mktime(&excessive) == -1 && std::memcmp(&saved, &excessive, sizeof(saved)) == 0, "mktime_failure_preserves_input");
    std::array<const GuestTm*, 8> slots{};
    std::array<std::thread, 8> workers;
    std::atomic<unsigned> arrived{}; std::atomic<bool> consistent{true};
    for (unsigned index = 0; index < workers.size(); ++index) {
        workers[index] = std::thread([&, index] {
            const std::int64_t seconds = static_cast<std::int64_t>(index) * 86400;
            slots[index] = Gmtime(&seconds);
            arrived.fetch_add(1);
            while (arrived.load() != workers.size()) { std::this_thread::yield(); }
            if (!slots[index] || slots[index]->mday != static_cast<int>(index + 1)) { consistent.store(false); }
        });
    }
    for (auto& worker : workers) { worker.join(); }
    for (unsigned a = 0; a < slots.size(); ++a) {
        for (unsigned b = a + 1; b < slots.size(); ++b) { if (slots[a] == slots[b]) { consistent.store(false); } }
    }
    check(consistent.load(), "per_thread_calendar_storage");
    std::printf("{\"event\":\"libc_calendar_self_test\",\"cases\":%u,\"passed\":%s}\n", cases, okay ? "true" : "false");
    return okay;
}
