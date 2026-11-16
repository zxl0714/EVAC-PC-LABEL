#pragma once
#include <cstdint>

namespace cave {
inline constexpr unsigned DeathLabelHostVersion = 1;

enum class DeathLabelResult : int { Failed, Completed, Exit, Closed };

// The caller owns the window, its message pump, and all pointers until return.
// 'prepared' and 'disc' come from the collection's verified PS2 data preparation.
struct DeathLabelHost {
    unsigned version = DeathLabelHostVersion;
    void* window{};
    void* closeEvent{};
    const wchar_t* prepared{};
    const wchar_t* disc{};
    const wchar_t* profile{};
    void* padContext{};
    void (*readPad)(void*, unsigned, std::uint32_t*, std::uint8_t*) noexcept{};
    float volume = 1.0f;
    bool vsync{};
    bool reflex = true;
    bool amdAntiLag{};
};

using RunDeathLabel = DeathLabelResult (*)(const DeathLabelHost*) noexcept;
inline constexpr const char* DeathLabelEntry = "EvacRunDeathLabel";
}
