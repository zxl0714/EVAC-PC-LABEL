#pragma once
#include <cstdint>

namespace cave {
inline constexpr unsigned DeathLabelHostVersion = 5;

enum class DeathLabelResult : int { Failed, Completed, Exit, Closed };

struct DeathLabelActions {
    // Active-high pad bits after the PS4 host has applied device bindings.
    std::uint16_t shot{};
    std::uint16_t bomb{};
    std::uint16_t rapid{};
};

struct DeathLabelScreen {
    // Playfield size and center offset in the PS4 title's 1920 × 1080 frame.
    int width = 672;
    int height = 896;
    int offsetX{};
    int offsetY{};
    unsigned quarterTurns{}; // Clockwise from the upright game image.
    bool gadgets = true;
};

struct DeathLabelAudio {
    unsigned music = 100;
    unsigned effects = 100;
};

// The caller owns the window, its message pump, and all pointers until return.
// 'prepared' and 'disc' come from the collection's verified PS2 data preparation.
struct DeathLabelHost {
    unsigned version = DeathLabelHostVersion;
    void* window{};
    void* closeEvent{};
    const wchar_t* prepared{};
    const wchar_t* disc{};
    const wchar_t* profile{};
    const wchar_t* gadgetSprites{};
    void* padContext{};
    void (*readPad)(void*, unsigned, std::uint32_t*, std::uint8_t*) noexcept{};
    float volume = 1.0f;
    bool vsync{};
    bool reflex = true;
    bool amdAntiLag{};
    const DeathLabelActions* actions{};
    DeathLabelScreen screen{};
    DeathLabelAudio audio{};
};

using RunDeathLabel = DeathLabelResult (*)(const DeathLabelHost*) noexcept;
inline constexpr const char* DeathLabelEntry = "EvacRunDeathLabel";
}
