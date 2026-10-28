#pragma once
#include "common/ini.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cave {
inline constexpr std::array<std::string_view,16> kButtons{
    "up","down","left","right","south","east","west","north","start","select","l1","r1","l2","r2","l3","r3"};
inline constexpr std::array<std::string_view,4> kRightStickButtons{
    "right_stick_up","right_stick_down","right_stick_left","right_stick_right"};
struct Binding {
    std::vector<std::uint16_t> keys;
    std::string text;
    std::size_t line{};
    std::string section;
};
struct Settings {
    bool fullscreen{}, vsync{true}, gamepad_enabled{true}, community_enabled{true};
    double deadzone{0.20}, volume{1.0};
    unsigned sdoj_frame_delay{1};
    unsigned g2_frame_delay{1};
    std::string log_level{"info"}, endpoint{"https://xiaolu.life/cave/api/v1"}, nickname{"Player"};
    std::array<Binding,16> keyboard;
    // Opt-in per-title axes: Guwange Xbox 360 mode independently moves Shikigami.
    std::array<Binding,4> keyboard_right_stick;
};
Settings ResolveSettings(const Ini& ini, std::string_view game_id);
// Public defaults only; do not accidentally edit a selected game's override.
Settings ResolveSharedSettings(const Ini& ini);
Settings PreviewSharedPreferences(std::string_view before, bool fullscreen, std::string_view nickname);
void SaveSharedPreferences(const fs::path& root, std::string_view before, bool fullscreen, std::string_view nickname);
Settings ParseSettings(std::string_view text, std::string_view game_id);
Settings LoadSettings(const fs::path& path, std::string_view game_id);
std::uint16_t KeyCode(std::string_view name);
bool ValidUtf8(std::string_view value);
std::string_view DefaultConfig();
// Read-only lookup: user-data/config.ini wins; accept the legacy root file.
fs::path ConfigPath(const fs::path& root);
// Explicit first-launch operation. Read-only inspection never calls this.
fs::path EnsureConfig(const fs::path& root);
}  // namespace cave
