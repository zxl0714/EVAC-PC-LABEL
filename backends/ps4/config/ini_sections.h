#pragma once
#include <stdexcept>
#include <string>
#include <string_view>

namespace kt::config {
inline constexpr std::size_t kMaximumConfigBytes = 64 * 1024;
struct Sections {
    std::string keyboard, community;
    bool has_keyboard{}, has_community{};
};
// Select known sections without interpreting domain values. Blank placeholders
// preserve original line numbers for both parsers. Never strip URI fragments or
// punctuation from values in the community section.
inline Sections SplitSections(std::string_view text) {
    if (text.size() > kMaximumConfigBytes || text.find('\0') != text.npos) {
        throw std::runtime_error("Input config exceeds 64 KiB or contains NUL bytes; use UTF-8");
    }
    if (text.starts_with("\xef\xbb\xbf")) { text.remove_prefix(3); }
    const auto trim = [](std::string_view value) {
        const auto first = value.find_first_not_of(" \t\r");
        return first == value.npos ? std::string_view{} : value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
    };
    Sections result;
    unsigned section{};
    std::size_t line_number{};
    while (!text.empty()) {
        ++line_number;
        const auto fail = [&](const char* reason) {
            throw std::runtime_error("Input config line " + std::to_string(line_number) + ": " + reason);
        };
        const auto end = text.find('\n');
        const auto original = text.substr(0, end);
        const auto newline = end == text.npos ? std::string_view{} : std::string_view("\n");
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (original.size() > 1024) { fail("line exceeds 1024 bytes"); }
        auto line = trim(original);
        bool header{};
        if (!line.empty() && line.front() == '[') {
            auto name = std::string(trim(line.substr(0, line.find_first_of(";#"))));
            for (auto& c : name) { if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c + ('a' - 'A')); } }
            if (name == "[keyboard]" && !result.has_keyboard) { section = 1; result.has_keyboard = true; }
            else if (name == "[community]" && !result.has_community) { section = 2; result.has_community = true; }
            else { fail("unknown or repeated section; use [keyboard] or [community]"); }
            header = true;
        } else if (!line.empty() && line.front() != ';' && line.front() != '#') {
            if (!section || line.find('=') == line.npos) { fail("place key = value inside a known section"); }
        }
        if (section == 1) { result.keyboard += header ? "[keyboard]" : original; }
        if (section == 2) { result.community += header ? "[community]" : original; }
        result.keyboard += newline; result.community += newline;
    }
    return result;
}
}
