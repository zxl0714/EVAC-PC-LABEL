#include "native_math.h"
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <limits>

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
float KT_SYSV StandardSinFloat(float value) {return Trig(value,0);}
float KT_SYSV StandardCosFloat(float value) {return Trig(value,1);}
double KT_SYSV StandardSinDouble(double value) {return Trig(value,0);}
double KT_SYSV StandardCosDouble(double value) {return Trig(value,1);}
void KT_SYSV SinCosFloat(float value,float* sine,float* cosine) {
    const auto s=Trig(value,0),c=Trig(value,1);
    *sine=s;*cosine=c;
}
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
float KT_SYSV StandardLogFloat(float value) {return Logarithm(value,0);}
float KT_SYSV StandardLogTenFloat(float value) {return Logarithm(value,1);}
double KT_SYSV StandardLogDouble(double value) {return Logarithm(value,0);}
double KT_SYSV StandardLogTenDouble(double value) {return Logarithm(value,1);}
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
    if (hash == "Q4rRL34CEeE") return reinterpret_cast<std::uint64_t>(&StandardSinFloat);
    if (hash == "-P6FNMzk2Kc") return reinterpret_cast<std::uint64_t>(&StandardCosFloat);
    if (hash == "H8ya2H00jbI") return reinterpret_cast<std::uint64_t>(&StandardSinDouble);
    if (hash == "2WE3BTYVwKM") return reinterpret_cast<std::uint64_t>(&StandardCosDouble);
    if (hash == "RQXLbdT2lc4") return reinterpret_cast<std::uint64_t>(&StandardLogFloat);
    if (hash == "lhpd6Wk6ccs") return reinterpret_cast<std::uint64_t>(&StandardLogTenFloat);
    if (hash == "rtV7-jWC6Yg") return reinterpret_cast<std::uint64_t>(&StandardLogDouble);
    if (hash == "WuMbPBKN1TU") return reinterpret_cast<std::uint64_t>(&StandardLogTenDouble);
    if (hash == "pztV4AF18iI") return reinterpret_cast<std::uint64_t>(&SinCosFloat);
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
bool MathSelfTest() {
    bool passed=true;
    const auto check=[&](bool okay,const char* name) {
        passed &= okay;
        std::printf("{\"event\":\"math_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n",name,okay?"true":"false");
    };
    using Single=float(KT_SYSV*)(float);
    using Double=double(KT_SYSV*)(double);
    const auto sine=reinterpret_cast<Single>(ResolveMathImport("Q4rRL34CEeE#A#A"));
    const auto cosine=reinterpret_cast<Single>(ResolveMathImport("-P6FNMzk2Kc#A#A"));
    const auto log=reinterpret_cast<Single>(ResolveMathImport("RQXLbdT2lc4#A#A"));
    const auto log10=reinterpret_cast<Single>(ResolveMathImport("lhpd6Wk6ccs#A#A"));
    const auto dsine=reinterpret_cast<Double>(ResolveMathImport("H8ya2H00jbI#A#A"));
    const auto dcosine=reinterpret_cast<Double>(ResolveMathImport("2WE3BTYVwKM#A#A"));
    const auto dlog=reinterpret_cast<Double>(ResolveMathImport("rtV7-jWC6Yg#A#A"));
    const auto dlog10=reinterpret_cast<Double>(ResolveMathImport("WuMbPBKN1TU#A#A"));
    check(sine(0.5f)==std::sin(0.5f) && cosine(0.5f)==std::cos(0.5f),"single_sysv_sse_lane");
    check(dsine(0.5)==std::sin(0.5) && dcosine(0.5)==std::cos(0.5),"double_sysv_sse_lane");
    check(log(1)==0 && log10(100)==2 && dlog(1)==0 && dlog10(100)==2,"natural_and_decimal_log_are_distinct");
    check(std::signbit(sine(-0.0f)) && std::signbit(dsine(-0.0)) && cosine(0)==1 && dcosine(0)==1,"signed_zero");
    const auto saved=errno;errno=73;
    const bool special=std::isnan(cosine((std::numeric_limits<float>::infinity)())) &&
        std::isinf(log(0)) && std::signbit(log(0)) && std::isnan(dlog(-1));
    check(special && errno==73,"special_values_do_not_overwrite_host_errno");errno=saved;
    using Combined=void(KT_SYSV*)(float,float*,float*);
    const auto combined=reinterpret_cast<Combined>(ResolveMathImport("pztV4AF18iI#A#A"));
    float results[]{73,0,0,73};combined(0.5f,&results[1],&results[2]);
    check(results[0]==73 && results[3]==73 && results[1]==std::sin(0.5f) && results[2]==std::cos(0.5f),
        "sincosf_separate_sse_input_and_integer_pointer_lanes");
    std::printf("{\"event\":\"math_self_test\",\"passed\":%s}\n",passed?"true":"false");return passed;
}
