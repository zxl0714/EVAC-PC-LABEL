#include "native_format.h"
#include "native_diagnostics.h"
#include "native_memory.h"
#include "native_async_log.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>
#include <cwchar>
#include <locale.h>
#include <cerrno>
#include <type_traits>
#include <algorithm>
#include <stdexcept>

#define KT_SYSV __attribute__((sysv_abi))
extern "C" int KT_SYSV NativeSysvSnprintf(char*, std::size_t, const char*, ...);
extern "C" int KT_SYSV NativeSysvPrintf(const char*, ...);
extern "C" int KT_SYSV NativeSysvSprintf(char*, const char*, ...);
extern "C" int KT_SYSV NativeSysvSwprintf(wchar_t*, std::size_t, const wchar_t*, ...);
extern "C" int KT_SYSV NativeSysvSscanf(const char*, const char*, ...);
extern "C" int KT_SYSV NativeSysvSprintfS(char*,std::size_t,const char*,...);
static_assert(sizeof(wchar_t) == 2, "The guest wide ABI has 16-bit units");

namespace {
struct SysvArguments {
    std::uint32_t gp_offset, fp_offset;
    const unsigned char* stack;
    const unsigned char* registers;
};
static_assert(sizeof(SysvArguments) == 24 && offsetof(SysvArguments, stack) == 8);
static_assert(sizeof(std::va_list) == sizeof(void*), "This adapter targets the Windows x64 CRT va_list");
struct FormatError { const char* name; std::uint64_t detail; };
[[noreturn]] void UnsupportedFormat(const char* name, std::uint64_t detail = 0) { throw FormatError{name, detail}; }

class ArgumentReader {
public:
    explicit ArgumentReader(const SysvArguments* input) {
        if (input) { arguments_ = *input; valid_ = true; }
    }
    std::uint64_t Next(bool floating = false) {
        if (!valid_ || arguments_.gp_offset > 48 || arguments_.gp_offset % 8 || arguments_.fp_offset < 48 ||
            arguments_.fp_offset > 176 || arguments_.fp_offset % 16) { UnsupportedFormat("native_format_va_list"); }
        const unsigned char* source{};
        auto& offset = floating ? arguments_.fp_offset : arguments_.gp_offset;
        const unsigned limit = floating ? 176 : 48;
        if (offset < limit) {
            if (!arguments_.registers) { UnsupportedFormat("native_format_register_save_area"); }
            source = arguments_.registers + offset;
            offset += floating ? 16 : 8;
        } else {
            const auto stack = reinterpret_cast<std::uintptr_t>(arguments_.stack);
            if (!stack || stack % 8 || stack > (std::numeric_limits<std::uintptr_t>::max)() - 8) {
                UnsupportedFormat("native_format_overflow_area", stack);
            }
            source = arguments_.stack;
            arguments_.stack += 8;
        }
        std::uint64_t value{};
        std::memcpy(&value, source, sizeof(value));
        return value;
    }
private:
    SysvArguments arguments_{};
    bool valid_{};
};
template<typename Character> struct ConvertedData {
    std::basic_string<Character> format;
    std::vector<std::uint64_t> slots;
};
using Converted = ConvertedData<char>;
template<typename Character> bool Digit(Character value) { return value >= '0' && value <= '9'; }
template<typename Character> bool In(std::string_view choices, Character value) {
    return value >= 0 && static_cast<unsigned>(value) < 128 && choices.find(static_cast<char>(value)) != std::string_view::npos;
}
template<typename Character> ConvertedData<Character> ConvertTyped(std::basic_string_view<Character> format, const SysvArguments* input) {
    ArgumentReader reader(input);
    ConvertedData<Character> converted;
    const auto append_ascii = [&](std::string_view value) { for (const auto c : value) { converted.format += static_cast<Character>(c); } };
    const auto take = [&](bool floating = false) {
        if (converted.slots.size() == 1024) { UnsupportedFormat("native_format_argument_limit"); }
        converted.slots.push_back(reader.Next(floating));
    };
    for (std::size_t i = 0; i < format.size();) {
        if (format[i] != '%') { converted.format += format[i++]; continue; }
        converted.format += '%'; ++i;
        if (i < format.size() && format[i] == '%') { converted.format += '%'; ++i; continue; }
        while (i < format.size() && In("-+ #0", format[i])) { converted.format += format[i++]; }
        const auto number = [&] {
            std::uint64_t value{};
            while (i < format.size() && Digit(format[i])) {
                value = value * 10 + static_cast<unsigned>(format[i] - '0');
                if (value > 0x7fffffff) { UnsupportedFormat("native_format_width_overflow", value); }
                converted.format += format[i++];
            }
        };
        if (i < format.size() && format[i] == '*') { take(); converted.format += '*'; ++i; }
        else { number(); }
        if (i < format.size() && format[i] == '.') {
            converted.format += '.'; ++i;
            if (i < format.size() && format[i] == '*') { take(); converted.format += '*'; ++i; }
            else { number(); }
        }
        std::string length;
        if (i < format.size() && In("hljztL", format[i])) {
            length += static_cast<char>(format[i++]);
            if (i < format.size() && (length == "h" || length == "l") && format[i] == length[0]) { length += format[i++]; }
        }
        if (i == format.size()) { UnsupportedFormat("native_format_incomplete_conversion"); }
        if (static_cast<unsigned>(format[i]) >= 128) { UnsupportedFormat("native_format_nonascii_conversion", static_cast<unsigned>(format[i])); }
        char type = static_cast<char>(format[i++]);
        if (std::string_view("diouxX").find(type) != std::string_view::npos) {
            if (length == "L") { UnsupportedFormat("native_format_integer_length"); }
            // The guest is LP64; Windows is LLP64. Normalize 64-bit integer
            // lengths before handing the converted slots to the Windows CRT.
            append_ascii(length == "l" || length == "ll" || length == "j" || length == "z" || length == "t" ? "ll" : length);
            take();
        } else if (std::string_view("fFeEgGaA").find(type) != std::string_view::npos) {
            if (!length.empty() && length != "l") { UnsupportedFormat("native_format_floating_length"); }
            take(true);
        } else if (type == 'p') {
            if (!length.empty()) { UnsupportedFormat("native_format_pointer_length"); }
            // The local libc's pointer field loads a qword and calls its
            // unsigned integer formatter with 'x' (libc +0x11826). It does
            // not use Windows %p's uppercase, fixed-16-digit spelling.
            append_ascii("ll"); type = 'x'; take();
        } else if (type == 's' || type == 'c') {
            if constexpr (std::is_same_v<Character, wchar_t>) {
                // The explicit wide forms have a known 16-bit ABI. The
                // unqualified multibyte forms need a separate locale contract.
                if (length != "l") { UnsupportedFormat("native_wide_format_multibyte_field", type); }
                append_ascii("l");
            } else if (!length.empty()) { UnsupportedFormat("native_format_wide_character"); }
            take();
        } else {
            // Positional args, %n, wide strings and extensions
            // need separate contracts; never let the CRT guess their layout.
            UnsupportedFormat("native_format_conversion", static_cast<unsigned char>(type));
        }
        converted.format += type;
    }
    return converted;
}
Converted Convert(std::string_view format, const SysvArguments* input) { return ConvertTyped<char>(format, input); }

struct ScanFormat { Converted converted; std::vector<std::size_t> sizes; };
ScanFormat ConvertScan(std::string_view format, const SysvArguments* input) {
    ArgumentReader reader(input); ScanFormat result;
    for (std::size_t i = 0; i < format.size();) {
        const auto value = format[i++]; result.converted.format += value;
        if (value != '%') { continue; }
        if (i < format.size() && format[i] == '%') { result.converted.format += format[i++]; continue; }
        const bool suppressed = i < format.size() && format[i] == '*';
        if (suppressed) { result.converted.format += format[i++]; }
        std::uint64_t width{}; const auto digits = i;
        while (i < format.size() && Digit(format[i])) {
            width = width * 10 + static_cast<unsigned>(format[i] - '0');
            if (width > 1024 * 1024) { UnsupportedFormat("native_scan_width_limit", width); }
            result.converted.format += format[i++];
        }
        if (i != digits && !width) { UnsupportedFormat("native_scan_zero_width"); }
        std::string length;
        if (i < format.size() && In("hljztL", format[i])) {
            length += format[i++];
            if (i < format.size() && (length == "h" || length == "l") && format[i] == length[0]) { length += format[i++]; }
        }
        if (i == format.size() || !In("diouxX", format[i]) || length == "L") { UnsupportedFormat("native_scan_conversion"); }
        const bool wide = length == "l" || length == "ll" || length == "j" || length == "z" || length == "t";
        result.converted.format += wide ? "ll" : length; result.converted.format += format[i++];
        if (!suppressed) {
            if (result.sizes.size() == 1024) { UnsupportedFormat("native_scan_argument_limit"); }
            result.converted.slots.push_back(reader.Next());
            result.sizes.push_back(wide ? 8 : length == "hh" ? 1 : length == "h" ? 2 : 4);
        }
    }
    return result;
}
std::string ScanString(const char* text, std::size_t limit) {
    std::string result;
    const auto address = reinterpret_cast<std::uintptr_t>(text);
    for (std::size_t offset = 0; offset <= limit; ++offset) {
        char value{}; SIZE_T copied{};
        if (!address || offset > UINTPTR_MAX - address || !ReadProcessMemory(GetCurrentProcess(),
            reinterpret_cast<const void*>(address + offset), &value, 1, &copied) || copied != 1) { throw std::invalid_argument("scan pointer"); }
        if (!value) { return result; }
        result += value;
    }
    UnsupportedFormat("native_scan_string_limit", limit);
}
__declspec(noinline) int Scan(const char* supplied, const char* format, const SysvArguments* input, const void* caller) {
    struct SavedErrno { int value = errno; ~SavedErrno() { errno = value; } } saved_error;
    try {
        const auto source = ScanString(supplied, 1024 * 1024), pattern = ScanString(format, 65536);
        auto converted = ConvertScan(pattern, input);
        std::vector<NativeHostWrite> leases;
        for (std::size_t index = 0; index < converted.sizes.size(); ++index) {
            auto lease = BeginNativeHostWrite(reinterpret_cast<void*>(converted.converted.slots[index]), converted.sizes[index]);
            if (!lease) { SetNativeErrno(22); return -1; }
            leases.push_back(std::move(lease));
        }
        // Reuse the same explicit SysV -> Windows x64 slot conversion as the
        // formatters. All scanning varargs are pointers, including spill args.
        auto arguments = reinterpret_cast<std::va_list>(converted.converted.slots.data());
        const auto result = std::vsscanf(source.c_str(), converted.converted.format.c_str(), arguments);
        static std::atomic<unsigned> traces{};
        if (traces.fetch_add(1) < 16) {
            TraceNativeApi("sscanf", static_cast<unsigned>(result), caller);
            std::printf("{\"event\":\"native_scan\",\"outputs\":%zu,\"assigned\":%d}\n", converted.sizes.size(), result);
        }
        return result;
    } catch (const FormatError& error) { UnsupportedApi(error.name, error.detail, caller); }
    catch (const std::invalid_argument&) { SetNativeErrno(22); return -1; }
    catch (const std::bad_alloc&) { SetNativeErrno(12); return -1; }
    catch (...) { UnsupportedApi("native_scan_host_failure", 0, caller); }
}

struct WideLocale {
    _locale_t value = _create_locale(LC_ALL, "C");
    ~WideLocale() { if (value) { _free_locale(value); } }
};
__declspec(noinline) int WideFormat(wchar_t* destination, std::size_t capacity, const wchar_t* format,
    const SysvArguments* input, const void* caller) {
    struct SavedErrno { int value = errno; ~SavedErrno() { errno = value; } } saved_error;
    if (!format || (!destination && capacity)) { SetNativeErrno(22); return -1; }
    try {
        std::size_t length{}; while (length <= 65536 && format[length]) { ++length; }
        if (length > 65536) { UnsupportedFormat("native_wide_format_format_limit", length); }
        const auto converted = ConvertTyped<wchar_t>({format, length}, input);
        static const WideLocale locale;
        if (!locale.value) { UnsupportedFormat("native_wide_format_locale"); }
        auto arguments = reinterpret_cast<std::va_list>(const_cast<std::uint64_t*>(converted.slots.data()));
        const auto required = _vscwprintf_l(converted.format.c_str(), locale.value, arguments);
        if (required < 0) { UnsupportedFormat("native_wide_format_host_failure"); }
        if (required > 16 * 1024 * 1024) { UnsupportedFormat("native_wide_format_output_limit", static_cast<unsigned>(required)); }
        std::vector<wchar_t> result(static_cast<std::size_t>(required) + 1);
        const auto written = _vsnwprintf_s_l(result.data(), result.size(), result.size() - 1, converted.format.c_str(), locale.value, arguments);
        if (written != required) { UnsupportedFormat("native_wide_format_changed", static_cast<unsigned>(written)); }
        const auto copied = capacity ? (std::min)(static_cast<std::size_t>(written), capacity - 1) : 0;
        if (capacity) { std::memcpy(destination, result.data(), copied * 2); destination[copied] = 0; }
        // Unlike snprintf, the local wide writer reserves the terminator and
        // returns -1 when any output does not fit. Capacity zero is not a query.
        const auto returned = static_cast<std::size_t>(written) == copied ? written : -1;
        static std::atomic<unsigned> logged{};
        if (logged.fetch_add(1) < 16) {
            TraceNativeApi("swprintf", static_cast<unsigned>(written), caller);
            std::printf("{\"event\":\"native_wide_format\",\"required_units\":%d,\"copied_units\":%zu,\"capacity\":%zu,\"result\":%d}\n", required, copied, capacity, returned);
        }
        return returned;
    } catch (const FormatError& error) {
        std::fputs("[wide format unsupported] first code units:", stderr);
        for (unsigned index = 0; index < 64 && format[index]; ++index) { std::fprintf(stderr, " %04x", static_cast<unsigned>(format[index])); }
        std::fputc('\n', stderr); UnsupportedApi(error.name, error.detail, caller);
    }
    catch (const std::bad_alloc&) { SetNativeErrno(12); return -1; }
    catch (...) { UnsupportedApi("native_wide_format_host_exception", 0, caller); }
}
int KT_SYSV Vswprintf(wchar_t* destination, std::size_t size, const wchar_t* format, const SysvArguments* input) {
    return WideFormat(destination, size, format, input, __builtin_return_address(0));
}
__declspec(noinline) int Format(char* destination, std::size_t size, const char* format, const SysvArguments* input, const void* caller, const char* api = "vsnprintf") {
    if (!format || (!destination && size)) { SetNativeErrno(22); return -1; }
    try {
        const auto length = strnlen_s(format, 65537);
        if (length > 65536) { UnsupportedFormat("native_format_format_limit", length); }
        auto converted = Convert(std::string_view(format, length), input);
        // All values now occupy the Windows x64 ABI's eight-byte vararg slots.
        // Use C99 vsnprintf, not legacy _vsnprintf (different truncation result).
        auto arguments = reinterpret_cast<std::va_list>(converted.slots.data());
        const auto result = std::vsnprintf(destination, size, converted.format.c_str(), arguments);
        if (result < 0) { UnsupportedFormat("native_format_host_failure"); }
        static std::atomic_flag traced = ATOMIC_FLAG_INIT;
        if (!traced.test_and_set()) { TraceNativeApi(api, static_cast<unsigned>(result), caller); }
        return result;
    } catch (const FormatError& error) { UnsupportedApi(error.name, error.detail, caller); }
    catch (const std::bad_alloc&) { SetNativeErrno(12); return -1; }
    catch (...) { UnsupportedApi("native_format_host_exception", 0, caller); }
}
int KT_SYSV Vsnprintf(char* destination, std::size_t size, const char* format, const SysvArguments* arguments) {
    return Format(destination, size, format, arguments, __builtin_return_address(0));
}
int SecureFormat(char* destination,std::size_t size,const char* format,const SysvArguments* arguments,const void* caller) {
    struct SavedErrno {int value=errno;~SavedErrno(){errno=value;}} saved;
    const auto constraint=[&](unsigned code) {
        static std::atomic<unsigned> count{};
        if (count.fetch_add(1)<16) NativeLogf("{\"event\":\"native_runtime_constraint\",\"api\":\"sprintf_s\",\"code\":%u}\n",code);
        // This supplied libc's default handler reports, then returns. The
        // wrapper returns zero, not the Windows CRT's negative error result.
        return 0;
    };
    if (!destination || !size || size>static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)())) return constraint(22);
    auto lease=BeginNativeHostWrite(destination,size);
    if (!lease) return constraint(22);
    if (!format) {destination[0]=0;return constraint(22);}
    const auto required=Format(nullptr,0,format,arguments,caller,"sprintf_s");
    if (required<0 || static_cast<std::size_t>(required)>=size) {destination[0]=0;return constraint(34);}
    return Format(destination,size,format,arguments,caller,"sprintf_s");
}
int UnboundedFormat(char* destination, const char* format, const SysvArguments* arguments, const void* caller, const char* api) {
    if (!destination) { SetNativeErrno(22); return -1; }
    const auto size = Format(nullptr, 0, format, arguments, caller, api);
    if (size < 0) { return size; }
    const auto result = Format(destination, static_cast<std::size_t>(size) + 1, format, arguments, caller, api);
    if (result != size) { UnsupportedApi("native_unbounded_format_changed", static_cast<unsigned>(result), caller); }
    return result;
}
int KT_SYSV Vsprintf(char* destination, const char* format, const SysvArguments* arguments) {
    return UnboundedFormat(destination, format, arguments, __builtin_return_address(0), "vsprintf");
}
__declspec(noinline) int ConsoleLine(const char* supplied, const void* caller) {
    const auto host_error = errno;
    struct RestoreErrno { int value; ~RestoreErrno() { errno = value; } } restore{host_error};
    try {
        std::string message = "[guest stdout] ";
        const auto address = reinterpret_cast<std::uintptr_t>(supplied);
        for (std::size_t offset = 0; offset < 1024 * 1024; ++offset) {
            char byte{}; SIZE_T received{};
            if (!address || address > UINTPTR_MAX - offset || !ReadProcessMemory(GetCurrentProcess(),
                reinterpret_cast<void*>(address + offset), &byte, 1, &received) || received != 1) {
                UnsupportedApi("puts_invalid_string", address, caller);
            }
            if (!byte) {
                message.push_back('\n');
                if (std::fwrite(message.data(), 1, message.size(), stderr) != message.size()) { SetNativeErrno(5); return -1; }
                return 0;
            }
            message.push_back(byte);
        }
        UnsupportedApi("puts_string_limit", 1024 * 1024, caller);
    } catch (const std::bad_alloc&) { SetNativeErrno(12); return -1; }
    catch (...) { UnsupportedApi("puts_host_failure", 0, caller); }
}
int KT_SYSV Puts(const char* text) { return ConsoleLine(text, __builtin_return_address(0)); }
}

extern "C" int NativeFormatCapture(const unsigned char* registers, const unsigned char* stack, const void* caller, unsigned kind) {
    std::array<std::uint64_t, 3> named{};
    std::memcpy(named.data(), registers, sizeof(named));
    if (kind == 0) {
        const SysvArguments arguments{24, 48, stack, registers};
        return Format(reinterpret_cast<char*>(named[0]), static_cast<std::size_t>(named[1]),
                      reinterpret_cast<const char*>(named[2]), &arguments, caller, "snprintf");
    }
    if (kind == 1) {
        const SysvArguments arguments{8, 48, stack, registers};
        const auto* format = reinterpret_cast<const char*>(named[0]);
        const auto size = Format(nullptr, 0, format, &arguments, caller, "printf");
        if (size < 0) { return size; }
        if (size == 0) { return 0; }
        if (size > 16 * 1024 * 1024) { UnsupportedApi("native_console_message_limit", static_cast<unsigned>(size), caller); }
        try {
            constexpr std::string_view prefix = "[guest stdout] ";
            std::vector<char> message(prefix.size() + static_cast<std::size_t>(size) + 1);
            std::memcpy(message.data(), prefix.data(), prefix.size());
            const auto written = Format(message.data() + prefix.size(), static_cast<std::size_t>(size) + 1,
                                        format, &arguments, caller, "printf");
            if (written != size) { UnsupportedApi("native_console_format_changed", static_cast<unsigned>(written), caller); }
            // Keep structured stdout JSON intact. The guest console has a
            // separate sink in the probe's stderr.log, including its prefix.
            if (std::fwrite(message.data(), 1, message.size() - 1, stderr) != message.size() - 1) { SetNativeErrno(5); return -1; }
            return written;
        } catch (const std::bad_alloc&) { SetNativeErrno(12); return -1; }
        catch (...) { UnsupportedApi("native_console_host_failure", 0, caller); }
    }
    if (kind == 2) {
        const SysvArguments arguments{16, 48, stack, registers};
        return UnboundedFormat(reinterpret_cast<char*>(named[0]), reinterpret_cast<const char*>(named[1]), &arguments, caller, "sprintf");
    }
    if (kind == 3) {
        const SysvArguments arguments{24, 48, stack, registers};
        return WideFormat(reinterpret_cast<wchar_t*>(named[0]), static_cast<std::size_t>(named[1]),
            reinterpret_cast<const wchar_t*>(named[2]), &arguments, caller);
    }
    if (kind == 4) {
        const SysvArguments arguments{16, 48, stack, registers};
        return Scan(reinterpret_cast<const char*>(named[0]), reinterpret_cast<const char*>(named[1]), &arguments, caller);
    }
    if (kind == 5) {
        const SysvArguments arguments{24,48,stack,registers};
        return SecureFormat(reinterpret_cast<char*>(named[0]),static_cast<std::size_t>(named[1]),
            reinterpret_cast<const char*>(named[2]),&arguments,caller);
    }
    UnsupportedApi("native_format_capture_kind", kind, caller);
}

std::uint64_t ResolveFormatImport(std::string_view identifier) {
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "Q2V+iqvjgC0") { return reinterpret_cast<std::uint64_t>(&Vsnprintf); }
    if (id == "eLdDw6l0-bU") { return reinterpret_cast<std::uint64_t>(&NativeSysvSnprintf); }
    if (id == "hcuQgD53UxM") { return reinterpret_cast<std::uint64_t>(&NativeSysvPrintf); }
    if (id == "jbz9I9vkqkk") { return reinterpret_cast<std::uint64_t>(&Vsprintf); }
    if (id == "tcVi5SivF7Q") { return reinterpret_cast<std::uint64_t>(&NativeSysvSprintf); }
    if (id == "nJz16JE1txM") { return reinterpret_cast<std::uint64_t>(&NativeSysvSwprintf); }
    if (id == "u0XOsuOmOzc") { return reinterpret_cast<std::uint64_t>(&Vswprintf); }
    if (id == "YQ0navp+YIc") { return reinterpret_cast<std::uint64_t>(&Puts); }
    if (id == "1Pk0qZQGeWo") { return reinterpret_cast<std::uint64_t>(&NativeSysvSscanf); }
    if (id == "xEszJVGpybs") { return reinterpret_cast<std::uint64_t>(&NativeSysvSprintfS); }
    return 0;
}
bool FormatSelfTest() {
    const auto TestFormat = &NativeSysvSnprintf;
    int passed{}; bool okay = true;
    const auto check = [&](bool result, const char* name) {
        std::printf("{\"event\":\"format_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
        okay &= result; if (result) { ++passed; }
    };
    std::array<char, 256> buffer{};
    check(ResolveFormatImport("xEszJVGpybs#A#A")==reinterpret_cast<std::uint64_t>(&NativeSysvSprintfS) &&
        NativeSysvSprintfS(buffer.data(),buffer.size(),"%s:%ld:%.1f:%d%d%d%d","ok",-4294967297ll,1.25,1,2,3,4)==23 &&
        !std::strcmp(buffer.data(),"ok:-4294967297:1.2:1234"),"secure_format_gp_fp_and_stack_capture");
    buffer.fill('q');
    check(NativeSysvSprintfS(buffer.data(),3,"%s","abc")==0 && buffer[0]==0 && buffer[3]=='q',
        "secure_overflow_clears_first_byte_and_returns_target_zero");
    buffer.fill('q');
    check(NativeSysvSprintfS(buffer.data(),0,"x")==0 && buffer[0]=='q' &&
        NativeSysvSprintfS(buffer.data(),4,nullptr)==0 && buffer[0]==0 && buffer[4]=='q',"secure_invalid_capacity_and_format");
    check(TestFormat(nullptr, 0, "name=%s", "Player") == 11, "zero_size_counts_required_bytes");
    buffer[0] = 'x'; buffer[1] = 'q';
    check(TestFormat(buffer.data(), 1, "%s", "abc") == 3 && buffer[0] == 0 && buffer[1] == 'q', "one_byte_buffer_and_external_sentinel");
    check(TestFormat(buffer.data(), 4, "%s", "abcdef") == 6 && !std::strcmp(buffer.data(), "abc"), "truncation_returns_full_length");
    check(TestFormat(buffer.data(), buffer.size(), "%s %% %c", "hello", 'Q') == 9 && !std::strcmp(buffer.data(), "hello % Q"), "string_character_percent");
    check(TestFormat(buffer.data(), buffer.size(), "%ld %zu %lld", -4294967297ll, 8589934593ull, -7ll) == 25 &&
          !std::strcmp(buffer.data(), "-4294967297 8589934593 -7"), "lp64_integer_lengths");
    check(TestFormat(buffer.data(), buffer.size(), "%hhd %hhu %#08x", 255, 511, 42) == 15 &&
          !std::strcmp(buffer.data(), "-1 255 0x00002a"), "promotions_and_integer_flags");
    check(TestFormat(buffer.data(), buffer.size(), "%*.*s", -7, 3, "abcdef") == 7 && !std::strcmp(buffer.data(), "abc    "), "star_width_precision_and_negative_width");
    check(TestFormat(buffer.data(), buffer.size(), "%d %.2f %d %.1f", 7, 1.25, 9, 2.5) == 12 &&
          !std::strcmp(buffer.data(), "7 1.25 9 2.5"), "interleaved_gp_fp");
    check(TestFormat(buffer.data(), buffer.size(), "%d%d%d%d%d%d%d%d", 1, 2, 3, 4, 5, 6, 7, 8) == 8 &&
          !std::strcmp(buffer.data(), "12345678"), "integer_overflow_stack");
    check(TestFormat(buffer.data(), buffer.size(), "%.0f%.0f%.0f%.0f%.0f%.0f%.0f%.0f%.0f", 1., 2., 3., 4., 5., 6., 7., 8., 9.) == 9 &&
          !std::strcmp(buffer.data(), "123456789"), "fp_registers_and_ninth_double_stack");
    check(TestFormat(buffer.data(), buffer.size(), "%d%d%d%d%.0f%.0f%.0f%.0f%.0f%.0f%.0f%.0f%.0f%d",
          1, 2, 3, 4, 1., 2., 3., 4., 5., 6., 7., 8., 9., 5) == 14 && !std::strcmp(buffer.data(), "12341234567895"),
          "interleaved_integer_and_fp_overflow_stack");
    check(TestFormat(buffer.data(), buffer.size(), "") == 0 && buffer[0] == 0 && Format(nullptr, 1, "x", nullptr, nullptr) == -1,
          "empty_format_and_invalid_destination");
    check(NativeSysvSprintf(buffer.data(), "%s:%ld:%.1f", "ok", -4294967297ll, 1.25) == 18 &&
          !std::strcmp(buffer.data(), "ok:-4294967297:1.2"), "sprintf_capture_and_full_output");
    check(TestFormat(buffer.data(), buffer.size(), "%p %p", reinterpret_cast<void*>(0x123456789abcdefull), nullptr) == 17 &&
          !std::strcmp(buffer.data(), "123456789abcdef 0"), "pointer_is_target_lowercase_hex_without_implicit_prefix_or_padding");
    check(TestFormat(buffer.data(), buffer.size(), "0x%*.*p", -12, 8, reinterpret_cast<void*>(42)) == 14 &&
          !std::strcmp(buffer.data(), "0x0000002a    "), "pointer_width_precision_and_explicit_prefix_follow_integer_rules");
    std::array<std::uint64_t, 6> registers{1, 2, 3, 4, 5, 6};
    const SysvArguments arguments{0, 48, nullptr, reinterpret_cast<const unsigned char*>(registers.data())};
    const auto copy = arguments;
    check(Format(buffer.data(), buffer.size(), "%d%d", &arguments, nullptr) == 2 && !std::memcmp(&arguments, &copy, sizeof(copy)), "va_list_copy_not_mutated");
    bool rejected = true;
    for (const char* format : {"%n", "%ls", "%Lf", "%2$s", "%lp", "%"}) {
        try { (void)Convert(format, &arguments); rejected = false; } catch (const FormatError&) {}
    }
    check(rejected, "unsupported_formats_rejected_before_crt");
    auto invalid = arguments; invalid.gp_offset = 1;
    bool bad_list = false;
    try { (void)Convert("%d", &invalid); } catch (const FormatError&) { bad_list = true; }
    check(bad_list, "invalid_va_list_rejected");
    std::array<wchar_t, 256> wide{};
    check(NativeSysvSwprintf(wide.data(), wide.size(), L"\x7d46=%04d", 21) == 6 && std::wcscmp(wide.data(), L"\x7d46=0021") == 0,
          "wide_unicode_literal_and_numeric_capture");
    check(NativeSysvSwprintf(wide.data(), wide.size(), L"%ld %zu %lld", -4294967297ll, 8589934593ull, -7ll) == 25 &&
          std::wcscmp(wide.data(), L"-4294967297 8589934593 -7") == 0, "wide_lp64_integer_lengths");
    check(NativeSysvSwprintf(wide.data(), wide.size(), L"%ls/%lc", L"\x7d46", 0xe9) == 3 && std::wcscmp(wide.data(), L"\x7d46/\xe9") == 0,
          "wide_explicit_string_and_character_are_sixteen_bit");
    wide.fill(0x7373);
    check(NativeSysvSwprintf(wide.data(), 4, L"abcdef") == -1 && std::wcscmp(wide.data(), L"abc") == 0 && wide[4] == 0x7373,
          "wide_truncation_returns_failure_and_keeps_prefix_and_tail");
    wide.fill(0x7373);
    check(NativeSysvSwprintf(wide.data(), 1, L"a") == -1 && wide[0] == 0 && wide[1] == 0x7373,
          "wide_one_unit_capacity_reserves_terminator");
    wide.fill(0x7373);
    check(NativeSysvSwprintf(wide.data(), 0, L"a") == -1 && NativeSysvSwprintf(nullptr, 0, L"") == 0 && wide[0] == 0x7373,
          "wide_zero_capacity_is_not_a_length_query");
    check(NativeSysvSwprintf(wide.data(), wide.size(), L"%d%d%d%d%d%d%d%d", 1, 2, 3, 4, 5, 6, 7, 8) == 8 && std::wcscmp(wide.data(), L"12345678") == 0,
          "wide_integer_overflow_stack");
    check(NativeSysvSwprintf(wide.data(), wide.size(), L"%.0f%.0f%.0f%.0f%.0f%.0f%.0f%.0f%.0f", 1., 2., 3., 4., 5., 6., 7., 8., 9.) == 9 && std::wcscmp(wide.data(), L"123456789") == 0,
          "wide_floating_registers_and_overflow_stack");
    check(NativeSysvSwprintf(wide.data(), wide.size(), L"%d %.2f %d %.1f", 7, 1.25, 9, 2.5) == 12 && std::wcscmp(wide.data(), L"7 1.25 9 2.5") == 0,
          "wide_interleaved_gp_and_fp");
    check(Vswprintf(wide.data(), wide.size(), L"%d%d", &arguments) == 2 && std::wcscmp(wide.data(), L"12") == 0 && std::memcmp(&arguments, &copy, sizeof(copy)) == 0,
          "wide_va_list_copy_is_not_mutated");
    const auto old_host_error = errno; errno = 73;
    const bool wide_errno_preserved = NativeSysvSwprintf(wide.data(), wide.size(), L"%.1f", 1.25) == 3 && errno == 73; errno = old_host_error;
    check(wide_errno_preserved, "wide_format_preserves_host_errno");
    bool wide_rejected = true;
    for (const auto* format : {L"%s", L"%c", L"%n", L"%Lf", L"%2$d", L"%", L"%lp"}) {
        try { (void)ConvertTyped<wchar_t>(format, &arguments); wide_rejected = false; } catch (const FormatError&) {}
    }
    check(wide_rejected, "wide_unknown_multibyte_and_unsafe_fields_are_explicit");
    std::printf("{\"event\":\"format_self_test\",\"passed\":%d,\"success\":%s}\n", passed, okay ? "true" : "false");
    return okay;
}
bool ScanSelfTest() {
    bool okay = true; unsigned cases{};
    const auto check = [&](bool value, const char* name) {
        std::printf("{\"event\":\"scan_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
        ++cases; okay &= value;
    };
    check(ResolveFormatImport("1Pk0qZQGeWo#tag") == reinterpret_cast<std::uint64_t>(&NativeSysvSscanf), "exact_sscanf_import");
    struct Guarded { int value = 73; unsigned guard = 0xa7a7a7a7; } first, second;
    check(NativeSysvSscanf("U3=-42", "U%d=%d", &first.value, &second.value) == 2 && first.value == 3 && second.value == -42 &&
          first.guard == 0xa7a7a7a7 && second.guard == 0xa7a7a7a7, "ranking_comment_fields_and_exact_four_byte_writes");
    first.value = second.value = 73;
    check(NativeSysvSscanf("U7=bad", "U%d=%d", &first.value, &second.value) == 1 && first.value == 7 && second.value == 73,
          "partial_match_preserves_second_output");
    first.value = 73;
    check(NativeSysvSscanf("X7=8", "U%d=%d", &first.value, &second.value) == 0 && first.value == 73, "literal_mismatch_is_zero_not_a_fabricated_assignment");
    check(NativeSysvSscanf("", "%d", &first.value) == -1 && first.value == 73 &&
          NativeSysvSscanf("  \t", "%d", &first.value) == -1, "eof_before_assignment");
    check(NativeSysvSscanf("  -12  +34", "%d %d", &first.value, &second.value) == 2 && first.value == -12 && second.value == 34,
          "whitespace_and_decimal_signs");
    unsigned hex{}; int automatic{};
    check(NativeSysvSscanf("17 0x2a 010", "%*d %x %i", &hex, &automatic) == 2 && hex == 42 && automatic == 8,
          "suppression_does_not_consume_a_pointer_and_integer_bases_follow_crt");
    check(NativeSysvSscanf("1234", "%2d%2d", &first.value, &second.value) == 2 && first.value == 12 && second.value == 34,
          "field_width_limits_each_conversion");
    std::int64_t signed_long{}; std::uint64_t size{};
    check(NativeSysvSscanf("-4294967297 8589934593", "%ld %zu", &signed_long, &size) == 2 && signed_long == -4294967297ll && size == 8589934593ull,
          "lp64_long_and_size_lengths_do_not_use_windows_32bit_long");
    struct Small { signed char byte{}; unsigned char guard0 = 0xa7; unsigned short word{}; unsigned short guard1 = 0xa7a7; short signed_word{}; unsigned short guard2 = 0xa7a7; } small;
    check(NativeSysvSscanf("127 65535 -32768", "%hhd %hu %hd", &small.byte, &small.word, &small.signed_word) == 3 &&
          small.byte == 127 && small.word == 65535 && small.signed_word == -32768 && small.guard0 == 0xa7 && small.guard1 == 0xa7a7 && small.guard2 == 0xa7a7,
          "hh_h_outputs_preserve_adjacent_sentinels");
    std::array<int, 8> integers{};
    check(NativeSysvSscanf("1 2 3 4 5 6 7 8", "%d%d%d%d%d%d%d%d", &integers[0], &integers[1], &integers[2], &integers[3],
                         &integers[4], &integers[5], &integers[6], &integers[7]) == 8 && integers == std::array{1,2,3,4,5,6,7,8},
          "sysv_register_and_overflow_stack_pointer_arguments");
    check(NativeSysvSscanf("100%", "%d%%", &first.value) == 1 && first.value == 100 && NativeSysvSscanf("", "") == 0,
          "literal_percent_and_empty_format");
    std::array<std::uint64_t, 6> registers{reinterpret_cast<std::uint64_t>(&first.value), reinterpret_cast<std::uint64_t>(&second.value)};
    const SysvArguments arguments{0, 48, nullptr, reinterpret_cast<const unsigned char*>(registers.data())};
    const auto before = arguments;
    check(Scan("41 42", "%d %d", &arguments, nullptr) == 2 && first.value == 41 && second.value == 42 && !std::memcmp(&arguments, &before, sizeof(before)),
          "va_list_reader_is_a_private_copy");
    auto* protected_value = static_cast<int*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!protected_value) { return false; }
    *protected_value = 73; DWORD previous{};
    if (!VirtualProtect(protected_value, 4096, PAGE_READONLY, &previous)) { VirtualFree(protected_value, 0, MEM_RELEASE); return false; }
    first.value = 73;
    check(NativeSysvSscanf("1 2", "%d%d", &first.value, protected_value) == -1 && first.value == 73 && *protected_value == 73,
          "all_destination_leases_are_validated_before_the_first_write");
    check(NativeSysvSscanf(nullptr, "%d", &first.value) == -1 && NativeSysvSscanf("1", nullptr, &first.value) == -1 &&
          NativeSysvSscanf(reinterpret_cast<const char*>(1), "%d", &first.value) == -1 && first.value == 73,
          "invalid_input_and_format_pointers_do_not_fault_or_write");
    bool rejected = true;
    for (const auto* pattern : {"%s", "%f", "%n", "%Ld", "%2$d", "%", "%0d", "%1048577d"}) {
        try { (void)ConvertScan(pattern, &arguments); rejected = false; } catch (const FormatError&) {}
    }
    check(rejected, "unsupported_or_unbounded_formats_are_rejected_before_host_crt");
    if (!VirtualProtect(protected_value, 4096, PAGE_READWRITE, &previous) || !VirtualFree(protected_value, 0, MEM_RELEASE)) { return false; }
    std::printf("{\"event\":\"scan_self_test\",\"passed\":%s,\"cases\":%u}\n", okay ? "true" : "false", cases);
    return okay;
}
