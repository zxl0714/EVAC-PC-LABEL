#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace espgaluda2 {
inline constexpr std::array<std::string_view, 16> kButtons{
    "up", "down", "left", "right", "a", "b", "x", "y", "start", "back", "lb", "rb", "lt", "rt", "ls", "rs"};
struct Binding {
    std::vector<std::uint16_t> keys;
    std::string text;
    std::size_t line{};
};
struct Settings {
    bool fullscreen{}, vsync{true}, gamepad_enabled{true}, community_enabled{};
    double deadzone{0.20}, volume{1.0};
    std::string log_level{"info"}, endpoint, nickname{"Player"};
    std::array<Binding, 16> keyboard;
};
Settings ParseSettings(std::string_view text);
Settings LoadSettings(const std::filesystem::path& path);
std::string ReadSettingsText(const std::filesystem::path& path);
std::uint16_t KeyCode(std::string_view name);
bool ValidUtf8(std::string_view value);
}
