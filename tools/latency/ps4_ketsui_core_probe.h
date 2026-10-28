#pragma once

// Included only in a private native_probe.cpp, after Runtime/GuestExport.
// Observe the original registered callback and late import binding. No original
// executable instruction is overwritten and all guest arguments are forwarded.
#include "ketsui-probe-pins.h"

namespace ketsui_core_probe {
using Frame = std::uintptr_t(KT_SYSV*)(void*, void*, void*);
using Apply = std::uintptr_t(KT_SYSV*)(void*, int, const void*, void*);
inline Frame original_frame{};
inline Apply original_apply{};
inline thread_local std::uint64_t current_frame{};

template<class T> T Read(const void* source, std::size_t offset = 0) {
    T value{};
    if (source) std::memcpy(&value, static_cast<const unsigned char*>(source) + offset, sizeof(value));
    return value;
}
inline std::uint64_t Point(std::uint64_t site, std::uint64_t pc,
    std::uint64_t a=0, std::uint64_t b=0, std::uint64_t c=0, std::uint64_t d=0) {
    return evac_latency::Record(evac_latency::Kind::GuestObservation, site, pc, a,b,c,d);
}
inline bool Matches(const Runtime& runtime, const std::array<unsigned char,32>& hash) {
    return runtime.header.source_sha256 == hash;
}
inline bool IsKetsui(const Runtime& runtime) {
    return runtime.image_name == "ketsui_01000.prx" && Matches(runtime, kKetsuiModuleSha);
}

std::uintptr_t KT_SYSV FrameProbe(void* context, void* inputs, void* output) {
    const auto begin = Point(100, 0x6610, reinterpret_cast<std::uintptr_t>(context),
        reinterpret_cast<std::uintptr_t>(inputs), reinterpret_cast<std::uintptr_t>(output),
        reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
    const auto previous = current_frame;
    current_frame = begin;
    const auto result = original_frame(context, inputs, output);
    current_frame = previous;
    const auto* surface = Read<const void*>(context);
    const auto dimensions = (std::uint64_t(Read<std::uint32_t>(surface,8))<<32) |
                             Read<std::uint32_t>(surface,12);
    Point(101, 0x6610, begin, result, Read<std::uintptr_t>(output,24), dimensions);
    return result;
}

std::uintptr_t KT_SYSV ApplyProbe(void* context, int player, const void* report, void* cache) {
    const auto type = Read<std::uint32_t>(report);
    const auto buttons = type == 3 ? Read<std::uint16_t>(report,8) : 0;
    const auto begin = Point(102, 0x5c0a0, current_frame, unsigned(player),
        (std::uint64_t(type)<<32)|buttons, reinterpret_cast<std::uintptr_t>(report));
    if (report) Point(104, 0x5c0a0, begin, Read<std::uint64_t>(report,8),
                     Read<std::uint64_t>(report,16), Read<std::uint64_t>(report,24));
    const auto result = original_apply(context,player,report,cache);
    Point(103, 0x5c0a0, begin, current_frame, unsigned(player), result);
    return result;
}

inline void Install(Runtime& runtime) {
    if (!evac_latency::state || !IsKetsui(runtime)) return;
    auto* slot = reinterpret_cast<std::uintptr_t*>(runtime.image+0x50088);
    const auto expected = reinterpret_cast<std::uintptr_t>(runtime.image+0x6610);
    if (*slot != expected) throw std::runtime_error("Private Ketsui frame callback pin differs");
    original_frame = reinterpret_cast<Frame>(expected);
    *slot = reinterpret_cast<std::uintptr_t>(&FrameProbe);
    Point(99,0x50088,reinterpret_cast<std::uintptr_t>(runtime.image),expected,*slot);
}

inline std::uint64_t WrapImport(Runtime& runtime, const Import& import, std::uint64_t target) {
    if (!evac_latency::state || !IsKetsui(runtime) || import.type!=2 ||
        import.nid.substr(0,import.nid.find('#'))!="w+dCT5vVoaQ") return target;
    const auto* provider = ImageAt(target);
    if (!provider || provider->image_name!="emu_integration_01400.prx" ||
        !Matches(*provider,kKetsuiIntegrationSha) ||
        target!=reinterpret_cast<std::uintptr_t>(provider->image+0x5c0a0))
        throw std::runtime_error("Private Ketsui input provider pin differs");
    original_apply = reinterpret_cast<Apply>(target);
    return reinterpret_cast<std::uintptr_t>(&ApplyProbe);
}
} // namespace ketsui_core_probe
