#include "native_rtc.h"
#include "native_memory.h"
#include "native_diagnostics.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <cstdio>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
// The RTC calendar is Gregorian, UTC, with microseconds since 0001-01-01.
// It is wall time, not the process/TSC clock used to measure intervals.
struct DateTime {
    std::uint16_t year, month, day, hour, minute, second;
    std::uint32_t microsecond;
};
static_assert(sizeof(DateTime) == 16 && offsetof(DateTime, microsecond) == 12);
constexpr std::uint64_t kDayUs = 86400000000ull;
constexpr auto kEpoch = std::chrono::sys_days{std::chrono::year{1}/1/1};
constexpr auto kFileEpoch = std::chrono::sys_days{std::chrono::year{1601}/1/1};
constexpr auto kLastDay = std::chrono::sys_days{std::chrono::year{9999}/12/31};
constexpr auto kFileEpochUs = static_cast<std::uint64_t>((kFileEpoch - kEpoch).count()) * kDayUs;
constexpr auto kMaxTick = (static_cast<std::uint64_t>((kLastDay - kEpoch).count()) + 1) * kDayUs - 1;
static_assert(kFileEpochUs == 0xb36168b6a58000ull);
static_assert(kMaxTick == 315537897599999999ull);
constexpr int kPointer = static_cast<int>(0x80b50002u);
constexpr int kValue = static_cast<int>(0x80b50003u);

template<class T> bool Read(const T* source, T& result) {
    SIZE_T count{};
    return source && ReadProcessMemory(GetCurrentProcess(), source, &result, sizeof(T), &count) && count == sizeof(T);
}
bool Writable(const void* address, std::size_t bytes) {
    return address && NativeMemoryWritable(address, bytes);
}
int CalendarTick(const DateTime& date, std::uint64_t& tick) {
    if (!date.year || date.year > 9999) { return static_cast<int>(0x80b50008u); }
    if (!date.month || date.month > 12) { return static_cast<int>(0x80b50009u); }
    const std::chrono::year_month_day day{std::chrono::year{date.year}, std::chrono::month{date.month}, std::chrono::day{date.day}};
    if (!day.ok()) { return static_cast<int>(0x80b5000au); }
    if (date.hour >= 24) { return static_cast<int>(0x80b5000bu); }
    if (date.minute >= 60) { return static_cast<int>(0x80b5000cu); }
    if (date.second >= 60) { return static_cast<int>(0x80b5000du); }
    if (date.microsecond >= 1000000) { return static_cast<int>(0x80b5000eu); }
    tick = static_cast<std::uint64_t>((std::chrono::sys_days{day} - kEpoch).count()) * kDayUs +
        static_cast<std::uint64_t>(date.hour) * 3600000000ull + static_cast<std::uint64_t>(date.minute) * 60000000ull +
        static_cast<std::uint64_t>(date.second) * 1000000 + date.microsecond;
    return 0;
}
DateTime CalendarDate(std::uint64_t tick) {
    const std::chrono::year_month_day date{kEpoch + std::chrono::days{static_cast<int>(tick / kDayUs)}};
    auto time = tick % kDayUs;
    DateTime result{static_cast<std::uint16_t>(int(date.year())), static_cast<std::uint16_t>(unsigned(date.month())),
                    static_cast<std::uint16_t>(unsigned(date.day())), static_cast<std::uint16_t>(time / 3600000000ull), 0, 0, 0};
    time %= 3600000000ull; result.minute = static_cast<std::uint16_t>(time / 60000000);
    time %= 60000000; result.second = static_cast<std::uint16_t>(time / 1000000);
    result.microsecond = static_cast<std::uint32_t>(time % 1000000);
    return result;
}
int HostUtcTick(std::uint64_t& tick) {
    FILETIME file{}; GetSystemTimePreciseAsFileTime(&file);
    const auto file_tick = (static_cast<std::uint64_t>(file.dwHighDateTime) << 32) | file.dwLowDateTime;
    tick = file_tick / 10 + kFileEpochUs;
    if (tick > kMaxTick) { return kValue; }
    return 0;
}
int KT_SYSV CurrentTick(std::uint64_t* output) {
    if (!Writable(output, sizeof(*output))) { return kPointer; }
    std::uint64_t tick{};
    if (const auto error = HostUtcTick(tick)) { return error; }
    std::memcpy(output, &tick, sizeof(tick));
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) { TraceNativeApi("sceRtcGetCurrentTick", tick, __builtin_return_address(0)); }
    return 0;
}
int KT_SYSV NetworkTickCompatibility(std::uint64_t* output) {
    if (!Writable(output, sizeof(*output))) { return kPointer; }
    std::uint64_t tick{};
    if (const auto error = HostUtcTick(tick)) { return error; }
    // PC offline application-clock policy. This supplies a real UTC date to
    // the title's unconditional script query; it does not attest network time
    // or create/login any PSN service. Do not reuse it as trusted server time.
    std::memcpy(output, &tick, sizeof(tick));
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) {
        std::printf("{\"event\":\"native_rtc_clock_source\",\"api\":\"sceRtcGetCurrentNetworkTick\","
                    "\"provider\":\"windows_utc\",\"policy\":\"offline_wall_clock_compatibility\","
                    "\"network_time_verified\":false,\"psn_authenticated\":false}\n");
        TraceNativeApi("sceRtcGetCurrentNetworkTick", tick, __builtin_return_address(0));
    }
    return 0;
}
int KT_SYSV SetTick(DateTime* output, const std::uint64_t* input) {
    std::uint64_t tick{};
    if (!Read(input, tick) || !Writable(output, sizeof(*output))) { return kPointer; }
    if (tick > kMaxTick) { return kValue; }
    const auto result = CalendarDate(tick);
    std::memcpy(output, &result, sizeof(result));
    return 0;
}
int KT_SYSV GetTick(const DateTime* input, std::uint64_t* output) {
    DateTime date{}; std::uint64_t tick{};
    if (!Read(input, date) || !Writable(output, sizeof(*output))) { return kPointer; }
    if (const auto error = CalendarTick(date, tick)) { return error; }
    std::memcpy(output, &tick, sizeof(tick));
    return 0;
}
int KT_SYSV Win32FileTime(const DateTime* input, std::uint64_t* output) {
    DateTime date{}; std::uint64_t tick{};
    if (!Read(input, date) || !Writable(output, sizeof(*output))) { return kPointer; }
    if (const auto error = CalendarTick(date, tick)) { return error; }
    // The established RTC contract clamps dates before the FILETIME epoch.
    const auto result = tick < kFileEpochUs ? 0 : (tick - kFileEpochUs) * 10;
    std::memcpy(output, &result, sizeof(result));
    return 0;
}
int KT_SYSV SetWin32FileTime(DateTime* output, std::uint64_t file_time) {
    if (!Writable(output, sizeof(*output))) { return kPointer; }
    if (file_time > (kMaxTick - kFileEpochUs) * 10 + 9) { return kValue; }
    const auto result = CalendarDate(file_time / 10 + kFileEpochUs);
    std::memcpy(output, &result, sizeof(result));
    return 0;
}
int LocalInZone(std::uint64_t tick, const DYNAMIC_TIME_ZONE_INFORMATION& zone, std::uint64_t& output) {
    if (tick > kMaxTick) { return kValue; }
    const auto date = CalendarDate(tick);
    const SYSTEMTIME utc{date.year, date.month, 0, date.day, date.hour, date.minute, date.second,
                         static_cast<WORD>(date.microsecond / 1000)};
    SYSTEMTIME local{};
    BOOL converted{};
    if (zone.TimeZoneKeyName[0]) {
        converted = SystemTimeToTzSpecificLocalTimeEx(&zone, &utc, &local);
    } else {
        // Fixed/custom rules have no registry key. The Ex API requires that
        // key on this Windows build; use the explicit-rule API instead.
        TIME_ZONE_INFORMATION rule{};
        rule.Bias = zone.Bias; rule.StandardBias = zone.StandardBias; rule.DaylightBias = zone.DaylightBias;
        rule.StandardDate = zone.StandardDate; rule.DaylightDate = zone.DaylightDate;
        converted = SystemTimeToTzSpecificLocalTime(&rule, &utc, &local);
    }
    if (!converted) { return kValue; }
    // Windows converts milliseconds; offsets are whole minutes. Preserve the
    // original six fractional digits rather than losing sub-ms RTC precision.
    return CalendarTick({local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute,
                         local.wSecond, date.microsecond}, output);
}
int KT_SYSV UtcToLocal(const std::uint64_t* input, std::uint64_t* output) {
    std::uint64_t tick{}, local{};
    if (!Read(input, tick) || !Writable(output, sizeof(*output))) { return kPointer; }
    if (tick > kMaxTick) { return kValue; }
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    if (GetDynamicTimeZoneInformation(&zone) == TIME_ZONE_ID_INVALID) { return kValue; }
    if (const auto error = LocalInZone(tick, zone, local)) { return error; }
    std::memcpy(output, &local, sizeof(local));
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) { TraceNativeApi("sceRtcConvertUtcToLocalTime", local, __builtin_return_address(0)); }
    return 0;
}
constexpr std::int64_t kUnixEpochSeconds = 62135596800ll;
bool SecondsTick(std::int64_t seconds, std::uint64_t& tick) {
    const auto shifted = static_cast<__int128_t>(seconds) + kUnixEpochSeconds;
    if (shifted < 0 || shifted > static_cast<__int128_t>(kMaxTick / 1000000)) { return false; }
    tick = static_cast<std::uint64_t>(shifted) * 1000000;
    return true;
}
bool ZoneRule(const DYNAMIC_TIME_ZONE_INFORMATION& zone, unsigned year, TIME_ZONE_INFORMATION& rule) {
    if (zone.TimeZoneKeyName[0] && !zone.DynamicDaylightTimeDisabled) {
        auto copy = zone;
        return GetTimeZoneInformationForYear(static_cast<USHORT>(year), &copy, &rule) != 0;
    }
    rule = {};
    rule.Bias = zone.Bias; rule.StandardBias = zone.StandardBias; rule.DaylightBias = zone.DaylightBias;
    rule.StandardDate = zone.StandardDate; rule.DaylightDate = zone.DaylightDate;
    return true;
}
bool LocalSecondsInZone(std::int64_t utc, const DYNAMIC_TIME_ZONE_INFORMATION& zone,
                        std::int64_t& local, int& daylight) {
    std::uint64_t tick{}, local_tick{};
    if (!SecondsTick(utc, tick) || LocalInZone(tick, zone, local_tick)) { return false; }
    const auto result = static_cast<std::int64_t>(local_tick / 1000000) - kUnixEpochSeconds;
    const auto date = CalendarDate(local_tick);
    TIME_ZONE_INFORMATION rule{};
    if (!ZoneRule(zone, date.year, rule)) { return false; }
    const bool has_dst = rule.StandardDate.wMonth && rule.DaylightDate.wMonth && rule.StandardBias != rule.DaylightBias;
    const auto standard_offset = -(static_cast<std::int64_t>(rule.Bias) + (has_dst ? rule.StandardBias : 0)) * 60;
    const auto daylight_offset = -(static_cast<std::int64_t>(rule.Bias) + rule.DaylightBias) * 60;
    const auto actual_offset = result - utc;
    if (actual_offset != standard_offset && (!has_dst || actual_offset != daylight_offset)) { return false; }
    local = result; daylight = has_dst && actual_offset == daylight_offset ? 1 : 0;
    return true;
}
bool UtcSecondsInZone(std::int64_t local, int daylight, const DYNAMIC_TIME_ZONE_INFORMATION& zone,
                      std::int64_t& utc, std::int64_t& normalized_local, int& normalized_daylight) {
    std::uint64_t local_tick{};
    if (!SecondsTick(local, local_tick)) { return false; }
    const auto date = CalendarDate(local_tick);
    TIME_ZONE_INFORMATION rule{};
    if (!ZoneRule(zone, date.year, rule)) { return false; }
    const bool has_dst = rule.StandardDate.wMonth && rule.DaylightDate.wMonth && rule.StandardBias != rule.DaylightBias;
    const auto standard = static_cast<__int128_t>(local) +
        (static_cast<std::int64_t>(rule.Bias) + (has_dst ? rule.StandardBias : 0)) * 60;
    const auto summer = static_cast<__int128_t>(local) +
        (static_cast<std::int64_t>(rule.Bias) + rule.DaylightBias) * 60;
    const auto minimum = -static_cast<__int128_t>(kUnixEpochSeconds);
    const auto maximum = static_cast<__int128_t>(kMaxTick / 1000000) - kUnixEpochSeconds;
    const bool standard_valid = standard >= minimum && standard <= maximum;
    const bool summer_valid = has_dst && summer >= minimum && summer <= maximum;
    const bool prefer_summer = daylight > 0 && has_dst;
    if (daylight >= 0 || !has_dst) {
        if (prefer_summer ? !summer_valid : !standard_valid) { return false; }
    } else if (!standard_valid && !summer_valid) { return false; }
    auto candidate = static_cast<std::int64_t>(prefer_summer || (!standard_valid && summer_valid) ? summer : standard);
    if (daylight < 0 && has_dst) {
        std::int64_t roundtrip{}; int state{};
        // Prefer standard time for a repeated local hour. If neither candidate
        // roundtrips (a spring gap), standard interpretation normalizes forward.
        if (!LocalSecondsInZone(candidate, zone, roundtrip, state) || roundtrip != local) {
            if (summer_valid && LocalSecondsInZone(static_cast<std::int64_t>(summer), zone, roundtrip, state) && roundtrip == local) {
                candidate = static_cast<std::int64_t>(summer);
            }
        }
    }
    std::int64_t normalized{}; int state{};
    if (!LocalSecondsInZone(candidate, zone, normalized, state)) { return false; }
    utc = candidate; normalized_local = normalized; normalized_daylight = state;
    return true;
}
}

bool NativeUtcClockSeconds(std::int64_t& output) {
    std::uint64_t tick{};
    if (HostUtcTick(tick)) { return false; }
    output = static_cast<std::int64_t>(tick / 1000000) - kUnixEpochSeconds;
    return true;
}
bool NativeUtcToLocalSeconds(std::int64_t utc, std::int64_t& local, int& daylight) {
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    return GetDynamicTimeZoneInformation(&zone) != TIME_ZONE_ID_INVALID && LocalSecondsInZone(utc, zone, local, daylight);
}
bool NativeLocalToUtcSeconds(std::int64_t local, int daylight, std::int64_t& utc,
                            std::int64_t& normalized_local, int& normalized_daylight) {
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    return GetDynamicTimeZoneInformation(&zone) != TIME_ZONE_ID_INVALID &&
        UtcSecondsInZone(local, daylight, zone, utc, normalized_local, normalized_daylight);
}

std::uint64_t ResolveRtcImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceRtc" || module != "libSceRtc") { return 0; }
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "18B2NS1y9UU") { return reinterpret_cast<std::uint64_t>(&CurrentTick); }
    if (hash == "ueega6v3GUw") { return reinterpret_cast<std::uint64_t>(&SetTick); }
    if (hash == "8w-H19ip48I") { return reinterpret_cast<std::uint64_t>(&GetTick); }
    if (hash == "jfRO0uTjtzA") { return reinterpret_cast<std::uint64_t>(&Win32FileTime); }
    if (hash == "zO9UL3qIINQ") { return reinterpret_cast<std::uint64_t>(&NetworkTickCompatibility); }
    if (hash == "n5JiAJXsbcs") { return reinterpret_cast<std::uint64_t>(&SetWin32FileTime); }
    if (hash == "M1TvFst-jrM") { return reinterpret_cast<std::uint64_t>(&UtcToLocal); }
    return 0;
}

bool RtcSelfTest() {
    bool okay = true; unsigned cases{};
    const auto check = [&](bool passed, const char* name) {
        std::printf("{\"event\":\"rtc_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, passed ? "true" : "false");
        ++cases; okay &= passed;
    };
    const auto local_case = [&](DateTime utc, DateTime expected, const DYNAMIC_TIME_ZONE_INFORMATION& zone, const char* name) {
        std::uint64_t input{}, want{}, output{};
        const bool valid = !CalendarTick(utc, input) && !CalendarTick(expected, want);
        check(valid && !LocalInZone(input, zone, output) && output == want, name);
    };
    DYNAMIC_TIME_ZONE_INFORMATION fixed{}; fixed.Bias = -480;
    local_case({2025,12,31,20,0,0,123456}, {2026,1,1,4,0,0,123456}, fixed, "utc_plus8_year_boundary_submillisecond");
    fixed.Bias = 210;
    local_case({2026,1,1,1,0,0,999999}, {2025,12,31,21,30,0,999999}, fixed, "utc_minus330_year_boundary");
    fixed.Bias = -345;
    local_case({2026,10,17,0,0,0,123}, {2026,10,17,5,45,0,123}, fixed, "utc_plus545_fractional_offset");
    DYNAMIC_TIME_ZONE_INFORMATION north{}; north.Bias = 300; north.DaylightBias = -60;
    north.DaylightDate.wMonth = 3; north.DaylightDate.wDay = 2; north.DaylightDate.wHour = 2;
    north.StandardDate.wMonth = 11; north.StandardDate.wDay = 1; north.StandardDate.wHour = 2;
    local_case({2026,3,8,6,59,59,123456}, {2026,3,8,1,59,59,123456}, north, "northern_dst_before_spring_gap");
    local_case({2026,3,8,7,0,0,123456}, {2026,3,8,3,0,0,123456}, north, "northern_dst_after_spring_gap");
    local_case({2026,11,1,5,59,59,999999}, {2026,11,1,1,59,59,999999}, north, "northern_dst_before_fall_repeat");
    local_case({2026,11,1,6,0,0,999999}, {2026,11,1,1,0,0,999999}, north, "northern_dst_after_fall_repeat");
    DYNAMIC_TIME_ZONE_INFORMATION south{}; south.Bias = -600; south.DaylightBias = -60;
    south.DaylightDate.wMonth = 10; south.DaylightDate.wDay = 1; south.DaylightDate.wHour = 2;
    south.StandardDate.wMonth = 4; south.StandardDate.wDay = 1; south.StandardDate.wHour = 3;
    local_case({2026,1,1,12,0,0,0}, {2026,1,1,23,0,0,0}, south, "southern_dst_summer");
    local_case({2026,7,1,12,0,0,0}, {2026,7,1,22,0,0,0}, south, "southern_dst_winter");
    const auto inverse_case = [&](DateTime local_date, int desired_dst, DateTime expected_utc,
                                  DateTime normalized_date, int expected_dst, const char* name) {
        std::uint64_t local_tick{}, utc_tick{}, normalized_tick{};
        const bool valid = !CalendarTick(local_date, local_tick) && !CalendarTick(expected_utc, utc_tick) &&
            !CalendarTick(normalized_date, normalized_tick);
        std::int64_t result{}, normalized{}; int actual_dst{};
        const auto local_seconds = static_cast<std::int64_t>(local_tick / 1000000) - kUnixEpochSeconds;
        check(valid && UtcSecondsInZone(local_seconds, desired_dst, north, result, normalized, actual_dst) &&
            result == static_cast<std::int64_t>(utc_tick / 1000000) - kUnixEpochSeconds &&
            normalized == static_cast<std::int64_t>(normalized_tick / 1000000) - kUnixEpochSeconds && actual_dst == expected_dst, name);
    };
    inverse_case({2026,7,1,12,0,0,0}, -1, {2026,7,1,16,0,0,0}, {2026,7,1,12,0,0,0}, 1, "inverse_auto_summer");
    inverse_case({2026,1,1,12,0,0,0}, -1, {2026,1,1,17,0,0,0}, {2026,1,1,12,0,0,0}, 0, "inverse_auto_winter");
    inverse_case({2026,3,8,2,30,0,0}, -1, {2026,3,8,7,30,0,0}, {2026,3,8,3,30,0,0}, 1, "inverse_gap_normalizes_forward");
    inverse_case({2026,11,1,1,30,0,0}, -1, {2026,11,1,6,30,0,0}, {2026,11,1,1,30,0,0}, 0, "inverse_fold_prefers_standard");
    inverse_case({2026,11,1,1,30,0,0}, 1, {2026,11,1,5,30,0,0}, {2026,11,1,1,30,0,0}, 1, "inverse_fold_explicit_daylight");
    inverse_case({2026,7,1,12,0,0,0}, 0, {2026,7,1,17,0,0,0}, {2026,7,1,13,0,0,0}, 1, "inverse_explicit_standard_normalization");
    DYNAMIC_TIME_ZONE_INFORMATION before{}, after{};
    const auto zone_result = GetDynamicTimeZoneInformation(&before);
    std::uint64_t now{}, local{};
    check(zone_result != TIME_ZONE_ID_INVALID && !HostUtcTick(now) && !UtcToLocal(&now, &local), "host_dynamic_zone_query");
    (void)GetDynamicTimeZoneInformation(&after);
    // Compare defined fields, not padding or unused string-buffer tails that
    // the Windows query need not initialize identically on repeated calls.
    const bool same_zone = before.Bias == after.Bias && before.StandardBias == after.StandardBias &&
        before.DaylightBias == after.DaylightBias && before.DynamicDaylightTimeDisabled == after.DynamicDaylightTimeDisabled &&
        std::memcmp(&before.StandardDate, &after.StandardDate, sizeof(SYSTEMTIME)) == 0 &&
        std::memcmp(&before.DaylightDate, &after.DaylightDate, sizeof(SYSTEMTIME)) == 0 &&
        std::wstring_view(before.StandardName) == std::wstring_view(after.StandardName) &&
        std::wstring_view(before.DaylightName) == std::wstring_view(after.DaylightName) &&
        std::wstring_view(before.TimeZoneKeyName) == std::wstring_view(after.TimeZoneKeyName);
    check(same_zone, "host_time_zone_settings_unchanged");
    std::uint64_t sentinel = 0xa5a5a5a5a5a5a5a5ull;
    check(LocalInZone(UINT64_MAX, fixed, sentinel) == kValue && sentinel == 0xa5a5a5a5a5a5a5a5ull,
          "invalid_zone_input_preserves_output");
    std::printf("{\"event\":\"rtc_self_test\",\"cases\":%u,\"passed\":%s}\n", cases, okay ? "true" : "false");
    return okay;
}
