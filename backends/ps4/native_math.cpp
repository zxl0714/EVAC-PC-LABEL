#include "native_math.h"
#include <cmath>
#include <cerrno>

#define KT_SYSV __attribute__((sysv_abi))
namespace {
float KT_SYSV RoundFloat(float value) { return std::round(value); }
// This libc's _FSin/_Sin take a boolean cosine selector, not a quadrant or
// scale argument. The local exports branch directly to cos[f] for any nonzero
// selector, and to sin[f] otherwise. Keep the SysV integer and SSE lanes apart.
template <typename T> __declspec(noinline) T Trig(T value, int cosine) {
    if (!std::isfinite(value)) { return value - value; }
    const auto host_error = errno;
    const auto result = cosine ? std::cos(value) : std::sin(value);
    errno = host_error;
    return result;
}
float KT_SYSV SinFloat(float value, int cosine) { return Trig(value, cosine); }
double KT_SYSV SinDouble(double value, int cosine) { return Trig(value, cosine); }
template <typename T> __declspec(noinline) T Power(T base, T exponent) {
    const auto host_error = errno;
    // The local libc's powf handles y=0 before NaNs, then propagates NaNs
    // arithmetically. In particular 1^NaN and (+/-1)^(+/-inf) are NaN here,
    // unlike the Windows/C99 special cases. It raises FP exceptions, not errno.
    T result;
    if (exponent == 0) { result = 1; }
    else if (std::isnan(base) || std::isnan(exponent)) { result = base + exponent; }
    else if (std::isinf(exponent) && std::abs(base) == 1) { volatile T infinity = exponent; result = infinity - infinity; }
    else { result = std::pow(base, exponent); }
    errno = host_error;
    return result;
}
float KT_SYSV PowFloat(float base, float exponent) { return Power(base, exponent); }
double KT_SYSV PowDouble(double base, double exponent) { return Power(base, exponent); }
template <typename T> __declspec(noinline) T Logarithm(T value, int selector) {
    const auto host_error = errno;
    // Verified local _Log dispatch: positive -> log10, zero -> log,
    // negative -> log2. The selector is not a scale or quadrant.
    const auto result = selector > 0 ? std::log10(value) : selector < 0 ? std::log2(value) : std::log(value);
    errno = host_error;
    return result;
}
double KT_SYSV LogDouble(double value, int selector) { return Logarithm(value, selector); }
float KT_SYSV LogFloat(float value, int selector) { return Logarithm(value, selector); }
__declspec(noinline) double ExpTwo(double value) {
    const auto host_error = errno;
    const auto result = std::exp2(value);
    errno = host_error;
    return result;
}
double KT_SYSV ExpTwoDouble(double value) { return ExpTwo(value); }
enum class Unary { Asin, Acos, Atan, Tan, Exp, Exp2 };
template <Unary operation> __declspec(noinline) float UnaryFloat(float value) {
    const auto host_error = errno;
    float result;
    if constexpr (operation == Unary::Asin) { result = std::asin(value); }
    else if constexpr (operation == Unary::Acos) { result = std::acos(value); }
    else if constexpr (operation == Unary::Atan) { result = std::atan(value); }
    else if constexpr (operation == Unary::Tan) { result = std::tan(value); }
    else if constexpr (operation == Unary::Exp) { result = std::exp(value); }
    else { result = std::exp2(value); }
    errno = host_error; return result;
}
float KT_SYSV AsinFloat(float value) { return UnaryFloat<Unary::Asin>(value); }
float KT_SYSV AcosFloat(float value) { return UnaryFloat<Unary::Acos>(value); }
float KT_SYSV AtanFloat(float value) { return UnaryFloat<Unary::Atan>(value); }
float KT_SYSV TanFloat(float value) { return UnaryFloat<Unary::Tan>(value); }
float KT_SYSV ExpFloat(float value) { return UnaryFloat<Unary::Exp>(value); }
float KT_SYSV ExpTwoFloat(float value) { return UnaryFloat<Unary::Exp2>(value); }
__declspec(noinline) float AtanTwo(float y, float x) {
    const auto host_error = errno; const auto result = std::atan2(y, x); errno = host_error; return result;
}
float KT_SYSV AtanTwoFloat(float y, float x) { return AtanTwo(y, x); }
template <typename T> __declspec(noinline) T Remainder(T left, T right) {
    const auto host_error = errno; const auto result = std::fmod(left, right); errno = host_error; return result;
}
float KT_SYSV RemainderFloat(float left, float right) { return Remainder(left, right); }
double KT_SYSV RemainderDouble(double left, double right) { return Remainder(left, right); }
}
std::uint64_t ResolveMathImport(std::string_view identifier) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "DDHG1a6+3q0") { return reinterpret_cast<std::uint64_t>(&RoundFloat); }
    if (hash == "ZtjspkJQ+vw") { return reinterpret_cast<std::uint64_t>(&SinFloat); }
    if (hash == "cCXjU72Z0Ow") { return reinterpret_cast<std::uint64_t>(&SinDouble); }
    if (hash == "1D0H2KNjshE") { return reinterpret_cast<std::uint64_t>(&PowFloat); }
    if (hash == "zXCi78bYrEI") { return reinterpret_cast<std::uint64_t>(&LogDouble); }
    if (hash == "jjjRS7l1MPM") { return reinterpret_cast<std::uint64_t>(&LogFloat); }
    if (hash == "dnaeGXbjP6E") { return reinterpret_cast<std::uint64_t>(&ExpTwoDouble); }
    if (hash == "QI-x0SL8jhw") { return reinterpret_cast<std::uint64_t>(&AcosFloat); }
    if (hash == "GZWjF-YIFFk") { return reinterpret_cast<std::uint64_t>(&AsinFloat); }
    if (hash == "EH-x713A99c") { return reinterpret_cast<std::uint64_t>(&AtanTwoFloat); }
    if (hash == "weDug8QD-lE") { return reinterpret_cast<std::uint64_t>(&AtanFloat); }
    if (hash == "ZE6RNL+eLbk") { return reinterpret_cast<std::uint64_t>(&TanFloat); }
    if (hash == "wuAQt-j+p4o") { return reinterpret_cast<std::uint64_t>(&ExpTwoFloat); }
    if (hash == "8zsu04XNsZ4") { return reinterpret_cast<std::uint64_t>(&ExpFloat); }
    if (hash == "pKwslsMUmSk") { return reinterpret_cast<std::uint64_t>(&RemainderDouble); }
    if (hash == "88Vv-AzHVj8") { return reinterpret_cast<std::uint64_t>(&RemainderFloat); }
    if (hash == "9LCjpWyQ5Zc") { return reinterpret_cast<std::uint64_t>(&PowDouble); }
    return 0;
}
