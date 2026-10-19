#include "native_profile.h"
#include "native_memory.h"
#include "native_diagnostics.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
constexpr int kParameter = static_cast<int>(0x80a10003u);
bool Writable(const void* address, std::size_t bytes) {
    return address && NativeMemoryWritable(address, bytes);
}
int KT_SYSV SystemParameter(int id, int* output) {
    if (!Writable(output, sizeof(*output))) { return kParameter; }
    // Development application settings, not a guess at the Windows language
    // or a fabricated PS4 system profile. Other parameter IDs are unsupported.
    int value{};
    if (id == 1) { value = kNativeProfile.language; }
    else if (id == 1000) { value = kNativeProfile.enter_button; }
    else { return kParameter; }
    std::memcpy(output, &value, sizeof(value));
    static std::atomic_flag traced_language = ATOMIC_FLAG_INIT, traced_button = ATOMIC_FLAG_INIT;
    auto& traced = id == 1 ? traced_language : traced_button;
    if (!traced.test_and_set()) { TraceNativeApi("sceSystemServiceParamGetInt", static_cast<unsigned>(id), __builtin_return_address(0)); }
    return 0;
}
int KT_SYSV ReceiveEvent(void* output) {
    // GetStatus reports zero pending events. There is no PS4 shell or online
    // event producer in this profile, and an empty dequeue writes nothing.
    if (!Writable(output, sizeof(std::int32_t))) { return kParameter; }
    return static_cast<int>(0x80a10004u);
}
int KT_SYSV PowerTick() {
    // One-shot idle reset only: no persistent power policy or global setting.
    if (!SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)) {
        UnsupportedApi("native_power_tick_failed", GetLastError(), __builtin_return_address(0));
    }
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) { TraceNativeApi("sceSystemServicePowerTick", 0, __builtin_return_address(0)); }
    return 0;
}
int KT_SYSV IsNeoMode() {
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) {
        TraceNativeApi("sceKernelIsNeoMode", kNativeProfile.neo, __builtin_return_address(0));
    }
    return kNativeProfile.neo ? 1 : 0;
}
int KT_SYSV GetSystemStatus(void* output) {
    if (!Writable(output, 9)) { return kParameter; }
    // Standalone runtime profile: no PS4 shell/dashboard, app suspension,
    // broadcast, or VR. Windows focus loss does not suspend guest execution.
    // CPU mode is the normal base-machine profile. There are currently no
    // system-service events to dequeue; do not invent installation/PSN events.
    // Only the nine established ABI bytes are written. The remaining caller
    // storage is reserved and is neither sized nor zeroed by this provider.
    constexpr std::byte status[9]{std::byte{}, std::byte{}, std::byte{}, std::byte{},
        std::byte{}, std::byte{}, std::byte{1}, std::byte{}, std::byte{}};
    std::memcpy(output, status, sizeof(status));
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) { TraceNativeApi("sceSystemServiceGetStatus", 0, __builtin_return_address(0)); }
    return 0;
}
int KT_SYSV DisplaySafeArea(void* output) {
    if (!Writable(output, sizeof(float))) { return kParameter; }
    // PC output has no TV overscan. Only ratio is an output; the caller's
    // remaining 128 reserved bytes are not ours to initialize or overwrite.
    std::memcpy(output, &kNativeProfile.display_safe_area_ratio, sizeof(float));
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) { TraceNativeApi("sceSystemServiceGetDisplaySafeAreaInfo", 0, __builtin_return_address(0)); }
    return 0;
}
}

std::uint64_t ResolveProfileImport(std::string_view identifier) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "WslcK1FQcGI") { return reinterpret_cast<std::uint64_t>(&IsNeoMode); }
    return 0;
}
std::uint64_t ResolveSystemStatusImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceSystemService" || module != "libSceSystemService") { return 0; }
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "fZo48un7LK4") { return reinterpret_cast<std::uint64_t>(&SystemParameter); }
    if (hash == "656LMQSrg6U") { return reinterpret_cast<std::uint64_t>(&ReceiveEvent); }
    if (hash == "XbbJC3E+L5M") { return reinterpret_cast<std::uint64_t>(&PowerTick); }
    if (library == "libSceSystemService" && module == "libSceSystemService" && identifier.substr(0, identifier.find('#')) == "rPo6tV8D9bM") {
        return reinterpret_cast<std::uint64_t>(&GetSystemStatus);
    }
    if (library == "libSceSystemService" && module == "libSceSystemService" && identifier.substr(0, identifier.find('#')) == "1n37q1Bvc5Y") {
        return reinterpret_cast<std::uint64_t>(&DisplaySafeArea);
    }
    return 0;
}
