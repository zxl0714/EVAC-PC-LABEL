#pragma once
#include <cstdint>
#include <string_view>

struct NativeProfile {
    const char* name;
    bool neo;
    std::uint64_t direct_bytes;
    std::uint32_t output_width, output_height;
    std::uint32_t vblank_numerator, vblank_denominator;
    float display_safe_area_ratio;
    std::int32_t language, enter_button;
};

// Startup assets plus the two movie arenas exceed the previous 4-GiB bound.
// This is a finite PC development budget, not a host-RAM/console-model probe.
inline constexpr NativeProfile kNativeProfile{"base-sdr-development", false, 9ull << 29, 1280, 720, 60, 1, 1.0f, 0, 0};
inline constexpr NativeProfile kNeoNativeProfile{"neo-sdr", true, 9ull << 29, 1920, 1080, 60, 1, 1.0f, 0, 0};
// Selected before guest entry. Output describes the virtual console display,
// independent of the user's Windows client size and host hardware model.
const NativeProfile& GetNativeProfile();
void ConfigureNativeProfile(bool neo);
std::uint64_t ResolveProfileImport(std::string_view identifier);
std::uint64_t ResolveSystemStatusImport(std::string_view identifier, std::string_view library, std::string_view module);
