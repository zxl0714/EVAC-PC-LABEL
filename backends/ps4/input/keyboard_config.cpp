#include "keyboard_config.h"
#include "../config/ini_sections.h"
#include "../community/protocol.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kt::input {
namespace {
constexpr std::size_t kMaximumBytes = 64 * 1024;
struct Button { std::string_view name; std::uint32_t mask; unsigned key; };
constexpr Button kButtons[] = {
    {"up", 0x10, VK_UP}, {"right", 0x20, VK_RIGHT},
    {"down", 0x40, VK_DOWN}, {"left", 0x80, VK_LEFT},
    {"cross", 0x4000, 'Z'}, {"circle", 0x2000, 'X'},
    {"square", 0x8000, 'C'}, {"triangle", 0x1000, 'V'},
    {"options", 8, VK_RETURN}, {"touchpad", 0x100000, VK_BACK},
    {"l1", 0x400, 'A'}, {"r1", 0x800, 'S'},
    {"l2", 0x100, 'Q'}, {"r2", 0x200, 'W'},
    {"l3", 2, 0}, {"r3", 4, 0},
};
constexpr std::size_t kButtonCount = std::size(kButtons);
std::string_view Trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) { return {}; }
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
std::string Lower(std::string_view value) {
    std::string result(value);
    for (auto& c : result) { if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c + ('a' - 'A')); } }
    return result;
}
[[noreturn]] void Error(std::size_t line, const std::string& reason) {
    throw std::runtime_error("Input config line " + std::to_string(line) + ": " + reason);
}
unsigned Key(std::string_view name, std::size_t line) {
    if (name.size() == 1) {
        const auto c = name.front();
        if (c >= 'a' && c <= 'z') { return static_cast<unsigned>(c - 'a' + 'A'); }
        if (c >= '0' && c <= '9') { return static_cast<unsigned>(c); }
    }
    constexpr std::pair<std::string_view, unsigned> named[] = {
        {"up", VK_UP}, {"right", VK_RIGHT}, {"down", VK_DOWN}, {"left", VK_LEFT},
        {"enter", VK_RETURN}, {"backspace", VK_BACK}, {"space", VK_SPACE},
        {"tab", VK_TAB}, {"escape", VK_ESCAPE}, {"home", VK_HOME}, {"end", VK_END},
        {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT}, {"insert", VK_INSERT}, {"delete", VK_DELETE},
        {"shift", VK_SHIFT}, {"ctrl", VK_CONTROL},
        {"multiply", VK_MULTIPLY}, {"add", VK_ADD}, {"subtract", VK_SUBTRACT},
        {"decimal", VK_DECIMAL}, {"divide", VK_DIVIDE},
    };
    for (const auto& [candidate, key] : named) { if (name == candidate) { return key; } }
    if (name.size() == 7 && name.starts_with("numpad") && name.back() >= '0' && name.back() <= '9') {
        return VK_NUMPAD0 + static_cast<unsigned>(name.back() - '0');
    }
    if (name.size() >= 2 && name.size() <= 3 && name.front() == 'f' && name[1] >= '1' && name[1] <= '9' &&
        (name.size() == 2 || (name[2] >= '0' && name[2] <= '9'))) {
        const auto number = static_cast<unsigned>(name[1] - '0') * (name.size() == 3 ? 10 : 1) +
            (name.size() == 3 ? static_cast<unsigned>(name[2] - '0') : 0);
        if (number == 10 || number == 11) { Error(line, "F10 and F11 are reserved for Windows/runtime shortcuts"); }
        if (number >= 1 && number <= 24) { return VK_F1 + number - 1; }
    }
    Error(line, "unknown key '" + std::string(name) + "'");
}
}
KeyboardBindings DefaultKeyboardBindings() {
    KeyboardBindings result{};
    for (const auto& button : kButtons) { if (button.key) { result[button.key] = button.mask; } }
    return result;
}
KeyboardBindings ParseKeyboardConfig(std::string_view text) {
    if (text.size() > kMaximumBytes) { throw std::runtime_error("Input config exceeds 64 KiB"); }
    if (text.find('\0') != std::string_view::npos) { throw std::runtime_error("Input config contains NUL bytes; use UTF-8, not UTF-16"); }
    if (text.starts_with("\xef\xbb\xbf")) { text.remove_prefix(3); }
    const auto sections = config::SplitSections(text);
    // Validate all requested network settings before publishing any bindings.
    // Loading settings alone neither starts a client nor changes NP state.
    if (sections.has_community) { (void)community::ParseConfig(sections.community); }
    text = sections.keyboard;
    std::array<std::vector<unsigned>, kButtonCount> keys;
    std::array<bool, kButtonCount> seen{};
    std::array<std::size_t, kButtonCount> entry_lines{};
    for (std::size_t i = 0; i < kButtonCount; ++i) { if (kButtons[i].key) { keys[i].push_back(kButtons[i].key); } }
    bool section = false;
    std::size_t line_number = 0;
    while (!text.empty()) {
        ++line_number;
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.size() > 1024) { Error(line_number, "line exceeds 1024 bytes"); }
        line = Trim(line.substr(0, line.find_first_of(";#")));
        if (line.empty()) { continue; }
        if (line.front() == '[') {
            if (Lower(line) != "[keyboard]") { Error(line_number, "expected [keyboard] section"); }
            if (section) { Error(line_number, "duplicate [keyboard] section"); }
            section = true;
            continue;
        }
        if (!section) { Error(line_number, "place bindings inside [keyboard]"); }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) { Error(line_number, "expected button = key"); }
        const auto button = Lower(Trim(line.substr(0, equals)));
        std::size_t index = 0;
        while (index < kButtonCount && kButtons[index].name != button) { ++index; }
        if (index == kButtonCount) { Error(line_number, "unknown button '" + button + "'"); }
        if (seen[index]) { Error(line_number, "duplicate button '" + button + "'"); }
        seen[index] = true;
        entry_lines[index] = line_number;
        keys[index].clear();
        auto value = Trim(line.substr(equals + 1));
        if (Lower(value) == "none") { continue; }
        while (true) {
            const auto comma = value.find(',');
            const auto token = Lower(Trim(value.substr(0, comma)));
            if (token.empty()) { Error(line_number, "empty key; use none to disable a button"); }
            if (keys[index].size() == 8) { Error(line_number, "at most eight keys per button"); }
            const auto key = Key(token, line_number);
            for (const auto previous : keys[index]) { if (previous == key) { Error(line_number, "repeated key '" + token + "'"); } }
            keys[index].push_back(key);
            if (comma == std::string_view::npos) { break; }
            value = value.substr(comma + 1);
        }
    }
    KeyboardBindings result{};
    for (std::size_t i = 0; i < kButtonCount; ++i) {
        for (const auto key : keys[i]) {
            if (result[key]) {
                std::size_t other = 0;
                while (other < i && kButtons[other].mask != result[key]) { ++other; }
                Error((std::max)(entry_lines[i], entry_lines[other]), "key conflict between '" +
                    std::string(kButtons[other].name) + "' and '" + std::string(kButtons[i].name) +
                    "'; rebind or disable one button (unlisted buttons keep their defaults)");
            }
            result[key] = kButtons[i].mask;
        }
    }
    return result;
}
KeyboardBindings LoadKeyboardConfig(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { throw std::runtime_error("Cannot open input config"); }
    std::string text(kMaximumBytes + 1, '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (stream.bad()) { throw std::runtime_error("Cannot read input config"); }
    text.resize(static_cast<std::size_t>(stream.gcount()));
    return ParseKeyboardConfig(text);
}
}
