#pragma once
#include <cstdint>

namespace cave {
inline constexpr unsigned DeathLabelHostVersion = 9;

struct DeathLabelKeyEvent {
    unsigned code{}; // Zero clears held keys after focus or session changes.
    bool down{};
    std::uint16_t buttons{}; // Mapped face and direction buttons from the PS4 host.
};

enum class DeathLabelResult : int { Failed, Completed, Exit, Closed };

struct DeathLabelActions {
    // Active-high pad bits after the PS4 host has applied device bindings.
    std::uint16_t shot{};
    std::uint16_t bomb{};
    std::uint16_t rapid{};
    bool operator==(const DeathLabelActions&) const = default;
};

struct DeathLabelScreen {
    // Playfield size and center offset in the PS4 title's 1920 × 1080 frame.
    int width = 672;
    int height = 896;
    int offsetX{};
    int offsetY{};
    unsigned quarterTurns{}; // Clockwise from the upright game image.
    bool gadgets = true;
    bool operator==(const DeathLabelScreen&) const = default;
};

struct DeathLabelAudio {
    unsigned music = 100;
    unsigned effects = 100;
    bool operator==(const DeathLabelAudio&) const = default;
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
    // Optional caller-owned slot for one parked session. Release it before
    // unloading the runtime, while no window handoff is in progress.
    void** session{};
    const wchar_t* controlsConfig{};
    void* keyContext{};
    unsigned (*readKeys)(void*,DeathLabelKeyEvent*,unsigned) noexcept{};
};

using RunDeathLabel = DeathLabelResult (*)(const DeathLabelHost*) noexcept;
using ReleaseDeathLabel = void (*)(void*) noexcept;
using WriteDeathLabelGraphicsTrace = void (*)(void*) noexcept;
inline constexpr const char* DeathLabelEntry = "EvacRunDeathLabel";
inline constexpr const char* DeathLabelRelease = "EvacReleaseDeathLabel";
inline constexpr const char* DeathLabelGraphicsTrace = "EvacWriteDeathLabelGraphicsTrace";
}
