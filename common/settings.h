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
struct Binding {
    std::vector<std::uint16_t> keys;
    std::string text;
    std::size_t line{};
    std::string section;
};
struct Settings {
    bool fullscreen{}, vsync{true}, gamepad_enabled{true}, community_enabled{true};
    double deadzone{0.20}, volume{1.0};
    std::string log_level{"info"}, endpoint{"https://xiaolu.life/cave/api/v1"}, nickname{"Player"};
    std::array<Binding,16> keyboard;
};
Settings ResolveSettings(const Ini& ini, std::string_view game_id);
Settings ParseSettings(std::string_view text, std::string_view game_id);
Settings LoadSettings(const fs::path& path, std::string_view game_id);
std::uint16_t KeyCode(std::string_view name);
bool ValidUtf8(std::string_view value);
std::string_view DefaultConfig();
// Explicit first-launch operation. Read-only inspection never calls this.
fs::path EnsureConfig(const fs::path& root);
}  // namespace cave
