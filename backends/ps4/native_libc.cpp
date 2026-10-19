#include "native_libc.h"
#include "native_diagnostics.h"
#include "native_host_timing.h"
#include <cstddef>
#include <cstring>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <locale.h>
#include <cerrno>
#include <cmath>
#include <atomic>
#include <limits>
#include <algorithm>

#define KT_SYSV __attribute__((sysv_abi))
extern "C" int KT_SYSV NativeSysvSetjmp(void*) __attribute__((returns_twice));
extern "C" void KT_SYSV NativeSysvLongjmp(void*, int) __attribute__((noreturn));
extern "C" [[noreturn]] void NativeJumpUnsupported(unsigned reason, const void* caller) {
    UnsupportedApi("longjmp_guest_signal_mask", reason, caller);
}

namespace {
void* KT_SYSV Fill(void* destination, int value, std::size_t size) {
    NativeHostOperationScope timing("memset", "memory", size, __builtin_return_address(0), size >= 65536);
    return size ? std::memset(destination, value, size) : destination;
}
void* KT_SYSV Copy(void* destination, const void* source, std::size_t size) {
    NativeHostOperationScope timing("memcpy", "memory", size, __builtin_return_address(0), size >= 65536);
    return size ? std::memcpy(destination, source, size) : destination;
}
void* KT_SYSV Move(void* destination, const void* source, std::size_t size) {
    NativeHostOperationScope timing("memmove", "memory", size, __builtin_return_address(0), size >= 65536);
    return size ? std::memmove(destination, source, size) : destination;
}
int KT_SYSV Compare(const void* first, const void* second, std::size_t size) {
    return size ? std::memcmp(first, second, size) : 0;
}
void* KT_SYSV FindByte(const void* data, int value, std::size_t size) {
    return size ? const_cast<void*>(std::memchr(data, value, size)) : nullptr;
}
constexpr std::array<std::int16_t, 257> CaseTable(bool lower) {
    std::array<std::int16_t, 257> result{}; result[0] = -1;
    for (unsigned value = 0; value < 256; ++value) {
        result[value + 1] = static_cast<std::int16_t>(lower && value >= 'A' && value <= 'Z' ? value + 32 :
            !lower && value >= 'a' && value <= 'z' ? value - 32 : value);
    }
    return result;
}
constexpr auto lower_table = CaseTable(true);
constexpr auto upper_table = CaseTable(false);
const std::int16_t* KT_SYSV LowerTable() { return lower_table.data() + 1; }
const std::int16_t* KT_SYSV UpperTable() { return upper_table.data() + 1; }
constexpr std::array<std::int16_t, 257> CTypeTable() {
    std::array<std::int16_t, 257> result{};
    for (unsigned value = 0; value < 256; ++value) {
        unsigned bits{};
        if (value < 32 || value == 127) { bits |= 0x80; }
        if (value >= 9 && value <= 13) { bits |= 0x40; }
        if (value == 9) { bits |= 0x400; }
        if (value == 32) { bits |= 4; }
        if (value >= '0' && value <= '9') { bits |= 0x21; }
        if (value >= 'A' && value <= 'Z') { bits |= 2; if (value <= 'F') { bits |= 1; } }
        if (value >= 'a' && value <= 'z') { bits |= 0x10; if (value <= 'f') { bits |= 1; } }
        if (value >= 33 && value <= 126 && !bits) { bits = 8; }
        result[value + 1] = static_cast<std::int16_t>(bits);
    }
    return result;
}
constexpr auto ctype_table = CTypeTable();
const std::int16_t* KT_SYSV ClassTable() { return ctype_table.data() + 1; }
struct NumericLocale {
    _locale_t value = _create_locale(LC_NUMERIC, "C");
    ~NumericLocale() { if (value) { _free_locale(value); } }
};
__declspec(noinline) double ParseDouble(const char* input, char** end, const void* caller) {
    static const NumericLocale locale;
    if (!locale.value || !input) { UnsupportedApi("strtod_c_locale_or_input", 0, caller); }
    const auto host_error = errno; errno = 0;
    const auto value = _strtod_l(input, end, locale.value);
    const auto result_error = errno; errno = host_error;
    if (result_error == ERANGE) { SetNativeErrno(34); }
    else if (result_error) { UnsupportedApi("strtod_host_error", static_cast<unsigned>(result_error), caller); }
    return value;
}
double KT_SYSV StringDouble(const char* input, char** end) { return ParseDouble(input, end, __builtin_return_address(0)); }
double KT_SYSV AsciiDouble(const char* input) { return ParseDouble(input, nullptr, __builtin_return_address(0)); }
float KT_SYSV StringFloat(const char* input, char** end) {
    static const NumericLocale locale;
    const auto caller = __builtin_return_address(0);
    if (!locale.value || !input) { UnsupportedApi("strtof_c_locale_or_input", 0, caller); }
    const auto host_error = errno; errno = 0;
    // Parse directly to float: strtod followed by a cast can double-round.
    const auto value = _strtof_l(input, end, locale.value);
    const auto result_error = errno; errno = host_error;
    if (result_error == ERANGE) { SetNativeErrno(34); }
    else if (result_error) { UnsupportedApi("strtof_host_error", static_cast<unsigned>(result_error), caller); }
    return value;
}
using SortComparison = int(KT_SYSV*)(const void*, const void*);
void KT_SYSV Sort(void* base, std::size_t count, std::size_t width, SortComparison compare) {
    if (count < 2 || !width) { return; }
    if (!base || !compare || count > (std::numeric_limits<std::size_t>::max)() / width) {
        UnsupportedApi("qsort_invalid_extent_or_comparator", count, __builtin_return_address(0));
    }
    // Allocation-free heapsort keeps comparator calls on the guest SysV ABI.
    // Windows CRT qsort cannot call a guest comparator directly.
    auto* bytes = static_cast<unsigned char*>(base);
    const auto element = [&](std::size_t index) { return bytes + index * width; };
    const auto exchange = [&](std::size_t first, std::size_t second) {
        auto* a = element(first); auto* b = element(second);
        for (std::size_t i = 0; i < width; ++i) { std::swap(a[i], b[i]); }
    };
    const auto sift = [&](std::size_t root, std::size_t end) {
        while (root < end / 2) {
            auto child = root * 2 + 1;
            if (child + 1 < end && compare(element(child), element(child + 1)) < 0) { ++child; }
            if (compare(element(root), element(child)) >= 0) { break; }
            exchange(root, child); root = child;
        }
    };
    for (auto start = count / 2; start;) { sift(--start, count); }
    for (auto end = count; end > 1;) { exchange(0, --end); sift(0, end); }
}
int KT_SYSV TestSortInt(const void* first, const void* second) {
    std::int32_t a{}, b{}; std::memcpy(&a, first, sizeof(a)); std::memcpy(&b, second, sizeof(b));
    return a < b ? INT32_MIN : a > b ? INT32_MAX : 0;
}
int KT_SYSV TestSortThreeBytes(const void* first, const void* second) { return std::memcmp(first, second, 3); }
__declspec(noinline) std::int64_t ParseLong(const char* input, char** end, int base, const void* caller) {
    static const NumericLocale locale;
    if (!locale.value || !input) { UnsupportedApi("strtol_c_locale_or_input", 0, caller); }
    if (base && (base < 2 || base > 36)) { if (end) { *end = const_cast<char*>(input); } SetNativeErrno(22); return 0; }
    const auto host_error = errno; errno = 0;
    const auto value = _strtoi64_l(input, end, base, locale.value);
    const auto result_error = errno; errno = host_error;
    if (result_error == ERANGE) { SetNativeErrno(34); }
    else if (result_error) { UnsupportedApi("strtol_host_error", static_cast<unsigned>(result_error), caller); }
    return value;
}
std::int64_t KT_SYSV StringLong(const char* input, char** end, int base) { return ParseLong(input, end, base, __builtin_return_address(0)); }
__declspec(noinline) std::uint64_t ParseUnsignedLong(const char* input, char** end, int base, const void* caller) {
    static const NumericLocale locale;
    if (!locale.value || !input) { UnsupportedApi("stoul_c_locale_or_input", 0, caller); }
    if (base && (base < 2 || base > 36)) { if (end) { *end = const_cast<char*>(input); } SetNativeErrno(22); return 0; }
    const auto host_error = errno; errno = 0;
    const auto value = _strtoui64_l(input, end, base, locale.value);
    const auto result_error = errno; errno = host_error;
    if (result_error == ERANGE) { SetNativeErrno(34); }
    else if (result_error) { UnsupportedApi("stoul_host_error", static_cast<unsigned>(result_error), caller); }
    return value;
}
std::uint64_t KT_SYSV StringUnsignedLong(const char* input, char** end, int base) { return ParseUnsignedLong(input, end, base, __builtin_return_address(0)); }
std::size_t KT_SYSV Length(const char* string) { return std::strlen(string); }
int KT_SYSV StringCompare(const char* first, const char* second) { return std::strcmp(first, second); }
int KT_SYSV StringCompareCount(const char* first, const char* second, std::size_t size) { return size ? std::strncmp(first, second, size) : 0; }
char* KT_SYSV StringCopyCount(char* destination, const char* source, std::size_t size) {
    std::size_t index{};
    while (index < size && source[index]) { destination[index] = source[index]; ++index; }
    while (index < size) { destination[index++] = 0; }
    return destination;
}
char* KT_SYSV StringFind(const char* input, int value) { return const_cast<char*>(std::strchr(input, value)); }
char* KT_SYSV StringCopy(char* destination, const char* source) {
    auto* cursor = destination;
    while ((*cursor++ = *source++) != 0) {}
    return destination;
}
char* KT_SYSV StringSubstring(const char* input, const char* needle) { return const_cast<char*>(std::strstr(input, needle)); }
std::size_t KT_SYSV StringLengthBounded(const char* input, std::size_t maximum) {
    std::size_t length{}; while (length < maximum && input[length]) { ++length; } return length;
}
int CaseCompare(const char* first, const char* second, std::size_t maximum) {
    for (std::size_t index = 0; index < maximum; ++index) {
        const auto a = lower_table[static_cast<unsigned char>(first[index]) + 1], b = lower_table[static_cast<unsigned char>(second[index]) + 1];
        if (a != b || !a) { return a - b; }
    }
    return 0;
}
int KT_SYSV StringCaseCompare(const char* first, const char* second) { return CaseCompare(first, second, SIZE_MAX); }
int KT_SYSV StringCaseCompareCount(const char* first, const char* second, std::size_t count) { return CaseCompare(first, second, count); }
thread_local char* token_cursor{};
char* KT_SYSV StringToken(char* input, const char* delimiters) {
    if (input) { token_cursor = input; }
    if (!token_cursor) { return nullptr; }
    token_cursor += std::strspn(token_cursor, delimiters);
    if (!*token_cursor) { token_cursor = nullptr; return nullptr; }
    auto* result = token_cursor; token_cursor += std::strcspn(token_cursor, delimiters);
    if (*token_cursor) { *token_cursor++ = 0; } else { token_cursor = nullptr; }
    return result;
}
std::atomic<std::uint64_t> random_state{1};
int KT_SYSV Random() {
    auto previous = random_state.load(std::memory_order_relaxed);
    std::uint64_t next{};
    do { next = previous * 6364136223846793005ull + 1; }
    while (!random_state.compare_exchange_weak(previous, next, std::memory_order_relaxed));
    return static_cast<int>((next >> 32) & 0x3fffffffu);
}
void KT_SYSV Seed(unsigned seed) { random_state.store(seed, std::memory_order_relaxed); }
}

std::uint64_t ResolveLibcMemoryImport(std::string_view identifier) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "8zTFvBIAIN8") { return reinterpret_cast<std::uint64_t>(&Fill); }
    if (hash == "Q3VBxCXhUHs") { return reinterpret_cast<std::uint64_t>(&Copy); }
    if (hash == "+P6FRGH4LfA") { return reinterpret_cast<std::uint64_t>(&Move); }
    if (hash == "DfivPArhucg" || hash == "5TjaJwkLWxE") { return reinterpret_cast<std::uint64_t>(&Compare); }
    if (hash == "8u8lPzUEq+U") { return reinterpret_cast<std::uint64_t>(&FindByte); }
    if (hash == "1uJgoVq3bQU") { return reinterpret_cast<std::uint64_t>(&LowerTable); }
    if (hash == "rcQCUr0EaRU") { return reinterpret_cast<std::uint64_t>(&UpperTable); }
    if (hash == "sUP1hBaouOw") { return reinterpret_cast<std::uint64_t>(&ClassTable); }
    if (hash == "2vDqwBlpF-o") { return reinterpret_cast<std::uint64_t>(&StringDouble); }
    if (hash == "SRI6S9B+-a4") { return reinterpret_cast<std::uint64_t>(&AsciiDouble); }
    if (hash == "xENtRue8dpI") { return reinterpret_cast<std::uint64_t>(&StringFloat); }
    if (hash == "AEJdIVZTEmo") { return reinterpret_cast<std::uint64_t>(&Sort); }
    if (hash == "mXlxhmLNMPg" || hash == "VOBg+iNwB-4") { return reinterpret_cast<std::uint64_t>(&StringLong); }
    if (hash == "zlfEH8FmyUA") { return reinterpret_cast<std::uint64_t>(&StringUnsignedLong); }
    if (hash == "kiZSXIWd9vg") { return reinterpret_cast<std::uint64_t>(&StringCopy); }
    if (hash == "viiwFMaNamA") { return reinterpret_cast<std::uint64_t>(&StringSubstring); }
    if (hash == "5jNubw4vlAA") { return reinterpret_cast<std::uint64_t>(&StringLengthBounded); }
    if (hash == "AV6ipCNa4Rw") { return reinterpret_cast<std::uint64_t>(&StringCaseCompare); }
    if (hash == "pXvbDfchu6k") { return reinterpret_cast<std::uint64_t>(&StringCaseCompareCount); }
    if (hash == "oVkZ8W8-Q8A") { return reinterpret_cast<std::uint64_t>(&StringToken); }
    if (hash == "cpCOXWMgha0") { return reinterpret_cast<std::uint64_t>(&Random); }
    if (hash == "VPbJwTCgME0") { return reinterpret_cast<std::uint64_t>(&Seed); }
    if (hash == "gNQ1V2vfXDE") { return reinterpret_cast<std::uint64_t>(&NativeSysvSetjmp); }
    if (hash == "lKEN2IebgJ0") { return reinterpret_cast<std::uint64_t>(&NativeSysvLongjmp); }
    if (hash == "j4ViWNHEgww") { return reinterpret_cast<std::uint64_t>(&Length); }
    if (hash == "Ovb2dSJOAuE") { return reinterpret_cast<std::uint64_t>(&StringCompare); }
    if (hash == "aesyjrHVWy4") { return reinterpret_cast<std::uint64_t>(&StringCompareCount); }
    if (hash == "6sJWiWSRuqk") { return reinterpret_cast<std::uint64_t>(&StringCopyCount); }
    if (hash == "ob5xAW4ln-0") { return reinterpret_cast<std::uint64_t>(&StringFind); }
    return 0;
}
bool LibcMemorySelfTest() {
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value; std::printf("{\"event\":\"libc_memory_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    const std::array<unsigned char, 5> bytes{1, 128, 255, 0, 128};
    using CompareFunction=int(KT_SYSV*)(const void*,const void*,std::size_t);
    const auto bcmp=reinterpret_cast<CompareFunction>(ResolveLibcMemoryImport("5TjaJwkLWxE#A#A"));
    check("bcmp_zero_nonzero_and_empty_sysv_contract",bcmp(bytes.data(),bytes.data(),bytes.size())==0 &&
        bcmp(bytes.data(),bytes.data()+1,1)!=0 && bcmp(nullptr,nullptr,0)==0);
    check("strtoll_uses_existing_64_bit_parser",ResolveLibcMemoryImport("VOBg+iNwB-4#A#A")==
        ResolveLibcMemoryImport("mXlxhmLNMPg#A#A"));
    check("memchr_returns_first_byte_pointer", FindByte(bytes.data(), 128, bytes.size()) == bytes.data() + 1);
    check("memchr_search_value_is_unsigned_byte", FindByte(bytes.data(), 0x180, bytes.size()) == bytes.data() + 1 && FindByte(bytes.data(), -1, bytes.size()) == bytes.data() + 2);
    check("memchr_honors_extent_and_embedded_zero", !FindByte(bytes.data(), 128, 1) && FindByte(bytes.data(), 0, bytes.size()) == bytes.data() + 3);
    check("zero_extent_does_not_dereference_null", !FindByte(nullptr, 0, 0) && !FindByte(bytes.data(), 1, 0));
    check("lowercase_table_is_direct_signed_short_with_eof", LowerTable()[-1] == -1 && LowerTable()['A'] == 'a' && LowerTable()['Z'] == 'z' &&
        LowerTable()['a'] == 'a' && LowerTable()[128] == 128 && LowerTable()[255] == 255);
    check("uppercase_table_preserves_eof_and_non_ascii_bytes", UpperTable()[-1] == -1 && UpperTable()['a'] == 'A' && UpperTable()['z'] == 'Z' &&
        UpperTable()['A'] == 'A' && UpperTable()[128] == 128 && UpperTable()[255] == 255);
    check("ctype_table_preserves_target_c_locale_bit_encoding", ClassTable()[-1] == 0 && ClassTable()['A'] == 3 && ClassTable()['G'] == 2 &&
        ClassTable()['a'] == 17 && ClassTable()['0'] == 33 && ClassTable()[' '] == 4 && ClassTable()['\t'] == 0x4c0 && ClassTable()[127] == 128 && ClassTable()[128] == 0);
    std::array<char, 6> target; target.fill(73);
    check("strncpy_zero_pads_only_the_requested_extent", StringCopyCount(target.data(), "ab", 5) == target.data() &&
        target[0] == 'a' && target[1] == 'b' && target[2] == 0 && target[4] == 0 && target[5] == 73);
    target.fill(73);
    check("strncpy_truncation_does_not_append_out_of_bounds_nul", StringCopyCount(target.data(), "abcdef", 3) == target.data() &&
        target[2] == 'c' && target[3] == 73);
    check("strncpy_zero_count_does_not_dereference_source", StringCopyCount(target.data(), nullptr, 0) == target.data() && target[3] == 73);
    char* end{};
    const char decimal[] = " \t-12.5suffix";
    check("strtod_c_locale_decimal_and_end_pointer", StringDouble(decimal, &end) == -12.5 && end == decimal + 7);
    check("strtod_hexadecimal_and_exponent", StringDouble("0x1.8p+1", nullptr) == 3.0 && StringDouble("1.25e2", nullptr) == 125.0);
    const char invalid[] = "not-a-number"; const auto old_error = errno; errno = 73;
    const bool unconverted = StringDouble(invalid, &end) == 0 && end == invalid && errno == 73; errno = old_error;
    check("strtod_no_conversion_preserves_start_pointer_and_host_errno", unconverted);
    check("strtod_special_values_and_signed_zero", std::isinf(StringDouble("-INF", nullptr)) && std::isnan(StringDouble("NAN", nullptr)) &&
        std::signbit(StringDouble("-0", nullptr)));
    check("strtod_range_results_are_not_truncated_float", std::isinf(StringDouble("1e9999", nullptr)) && StringDouble("1e-9999", nullptr) == 0 &&
        StringDouble("1e100", nullptr) == 1e100);
    check("atof_reuses_c_locale_strtod_without_an_end_pointer", AsciiDouble(" \t-12.5suffix") == -12.5 &&
        AsciiDouble("0x1.8p+1") == 3 && AsciiDouble("not-a-number") == 0 && std::signbit(AsciiDouble("-0")) &&
        AsciiDouble("9007199254740991") == 9007199254740991.0);
    check("strtof_direct_rounding_not_double_then_float", StringFloat("1.0000000596046448", nullptr) == std::nextafter(1.0f, 2.0f));
    check("strtof_c_locale_end_pointer_specials_and_range", StringFloat(decimal, &end) == -12.5f && end == decimal + 7 &&
        StringFloat("0x1.8p+1", nullptr) == 3 && std::signbit(StringFloat("-0", nullptr)) &&
        std::isnan(StringFloat("NAN", nullptr)) && std::isinf(StringFloat("1e99", nullptr)) && StringFloat("1e-99", nullptr) == 0);
    errno = 73;
    const bool float_unconverted = StringFloat(invalid, &end) == 0 && end == invalid && errno == 73;
    errno = old_error;
    check("strtof_no_conversion_preserves_start_and_host_errno", float_unconverted);
    std::array<std::int32_t, 9> sorted{73, INT32_MAX, -1, 0, INT32_MIN, 7, 7, -7, 73};
    Sort(sorted.data() + 1, 7, sizeof(sorted[0]), TestSortInt);
    check("qsort_sysv_callback_extreme_results_duplicates_and_sentinels", sorted ==
        std::array<std::int32_t, 9>{73, INT32_MIN, -7, -1, 0, 7, 7, INT32_MAX, 73});
    std::array<unsigned char, 11> triples{73, 3, 2, 1, 0, 8, 9, 3, 1, 2, 73};
    Sort(triples.data() + 1, 3, 3, TestSortThreeBytes);
    check("qsort_unaligned_three_byte_elements_preserve_neighbors", triples ==
        std::array<unsigned char, 11>{73, 0, 8, 9, 3, 1, 2, 3, 2, 1, 73});
    Sort(nullptr, 0, sizeof(int), nullptr); Sort(nullptr, 1, sizeof(int), nullptr); Sort(nullptr, SIZE_MAX, 0, nullptr);
    check("qsort_empty_single_and_zero_width_do_not_dereference", true);
    const char search[] = "a.b.c";
    check("strchr_first_match_terminator_and_absence", StringFind(search, '.') == search + 1 && StringFind(search, 0) == search + 5 && !StringFind(search, '/'));
    target.fill(73);
    check("strcpy_includes_only_the_source_terminator", StringCopy(target.data(), "abc") == target.data() && std::memcmp(target.data(), "abc\0", 4) == 0 && target[4] == 73);
    check("strstr_returns_first_occurrence_and_empty_needle", StringSubstring(search, ".b") == search + 1 && StringSubstring(search, "") == search && !StringSubstring(search, "xyz"));
    check("strnlen_never_reads_past_the_supplied_extent", StringLengthBounded(search, 3) == 3 && StringLengthBounded(search, 10) == 5 && StringLengthBounded(nullptr, 0) == 0);
    check("case_comparison_uses_target_c_locale_and_exact_count", StringCaseCompare("aBc", "AbC") == 0 && StringCaseCompare("AbD", "abc") > 0 &&
        StringCaseCompareCount("abX", "ABY", 2) == 0 && StringCaseCompareCount(nullptr, nullptr, 0) == 0);
    char tokens[] = ",a,,bc;d";
    const auto first = StringToken(tokens, ",;"); const auto second = StringToken(nullptr, ",;"); const auto third = StringToken(nullptr, ",;");
    check("strtok_skips_adjacent_delimiters_and_tracks_thread_state", first == tokens + 1 && std::strcmp(first, "a") == 0 &&
        second == tokens + 4 && std::strcmp(second, "bc") == 0 && third == tokens + 7 && !StringToken(nullptr, ",;"));
    check("strtol_uses_guest_64_bit_long_not_windows_long", StringLong("4294967297", nullptr, 10) == 4294967297ll &&
        StringLong("-9223372036854775808", nullptr, 10) == INT64_MIN && StringLong("9223372036854775808", nullptr, 10) == INT64_MAX);
    const char integer[] = " \t-0x10tail";
    check("strtol_base_detection_end_pointer_and_no_conversion", StringLong(integer, &end, 0) == -16 && end == integer + 7 &&
        StringLong(invalid, &end, 10) == 0 && end == invalid && StringLong("z", nullptr, 36) == 35);
    check("stoul_uses_guest_64_bit_unsigned_long_and_negative_wrap", StringUnsignedLong("4294967297", nullptr, 10) == 4294967297ull &&
        StringUnsignedLong("18446744073709551615", nullptr, 10) == UINT64_MAX && StringUnsignedLong("-1", nullptr, 10) == UINT64_MAX);
    const char unsigned_value[] = "0x100000000tail";
    check("stoul_base_end_pointer_and_overflow", StringUnsignedLong(unsigned_value, &end, 0) == 0x100000000ull && end == unsigned_value + 11 &&
        StringUnsignedLong("18446744073709551616", nullptr, 10) == UINT64_MAX);
    Seed(1); const std::array expected_random{408024109, 11635919, 196474438, 117649705, 812669700};
    bool random_exact = true; for (const auto value : expected_random) { random_exact &= Random() == value; }
    check("rand_matches_target_64_bit_state_and_30_bit_results", random_exact);
    Seed(0); const bool seed_zero = Random() == 0 && Random() == 408024109;
    Seed(0xffffffffu);
    check("srand_zero_and_unsigned_seed_are_not_replaced_by_windows_crt", seed_zero && Random() == 876841727 && Random() == 830605684);
    Seed(1);
    std::printf("{\"event\":\"libc_memory_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
