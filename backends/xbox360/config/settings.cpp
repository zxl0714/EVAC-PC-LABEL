#include "settings.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace espgaluda2 {
namespace {
std::string_view Trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r");
    if (begin == text.npos) return {};
    return text.substr(begin, text.find_last_not_of(" \t\r") - begin + 1);
}
std::string Lower(std::string_view text) {
    std::string result(text);
    for (auto& c : result) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return result;
}
[[noreturn]] void Error(std::size_t line, std::string_view section, std::string_view key, std::string_view reason) {
    throw std::runtime_error("config.ini line " + std::to_string(line) + " [" + std::string(section) + "]." +
                             std::string(key) + ": " + std::string(reason));
}
const std::map<std::string, std::uint16_t>& KeyNames() {
    static const auto names = [] {
        std::map<std::string, std::uint16_t> result{
            {"enter",13}, {"return",13}, {"space",32}, {"tab",9}, {"backspace",8}, {"escape",27},
            {"up",38}, {"down",40}, {"left",37}, {"right",39}, {"shift",16}, {"control",17}, {"alt",18},
            {"insert",45}, {"delete",46}, {"home",36}, {"end",35}, {"pageup",33}, {"pagedown",34},
            {"minus",189}, {"plus",187}, {"comma",188}, {"period",190}, {"semicolon",186},
            {"slash",191}, {"backslash",220}, {"lbracket",219}, {"rbracket",221}, {"quote",222},
            {"backtick",192}, {"numpadplus",107}, {"numpadminus",109}, {"numpadstar",106},
            {"numpadslash",111}, {"pause",19}, {"capslock",20}, {"numlock",144}, {"scrolllock",145}};
        for (int c = 'a'; c <= 'z'; ++c) result[std::string(1, char(c))] = c - 'a' + 'A';
        for (int c = '0'; c <= '9'; ++c) result[std::string(1, char(c))] = c;
        for (int n = 1; n <= 24; ++n) result["f" + std::to_string(n)] = 111 + n;
        for (int n = 0; n <= 9; ++n) result["numpad" + std::to_string(n)] = 96 + n;
        return result;
    }();
    return names;
}
void ParseBinding(Binding& binding, std::string_view value, std::size_t line, std::string_view button) {
    binding = Binding{{}, std::string(value), line};
    if (Lower(value) == "none") return;
    if (value.empty()) Error(line,"keyboard",button,"use none to disable a binding");
    std::set<std::uint16_t> seen;
    while (true) {
        const auto comma = value.find(',');
        const auto token = Trim(value.substr(0, comma));
        const auto code = KeyCode(token);
        if (!code) Error(line,"keyboard",button,"unknown key: " + std::string(token));
        if (code == 114 || code == 115 || code == 118 || code == 119 || code == 122 || code == 192)
            Error(line,"keyboard",button,"key is reserved for host diagnostics or fullscreen");
        if (!seen.insert(code).second) Error(line,"keyboard",button,"duplicate key or key alias");
        binding.keys.push_back(code);
        if (binding.keys.size() > 8) Error(line,"keyboard",button,"at most eight keys per button");
        if (comma == value.npos) break;
        value.remove_prefix(comma + 1);
    }
}
}

bool ValidUtf8(std::string_view value) {
    for (std::size_t pos = 0; pos < value.size();) {
        const auto start = static_cast<unsigned char>(value[pos++]);
        if (start < 128) {
            if (!start || (start < 32 && start != '\n' && start != '\r' && start != '\t')) return false;
            continue;
        }
        unsigned count{}, code{}, minimum{};
        if (start >= 0xc2 && start <= 0xdf) { count=1; code=start&31; minimum=0x80; }
        else if (start >= 0xe0 && start <= 0xef) { count=2; code=start&15; minimum=0x800; }
        else if (start >= 0xf0 && start <= 0xf4) { count=3; code=start&7; minimum=0x10000; }
        else return false;
        if (pos + count > value.size()) return false;
        for (unsigned n = 0; n < count; ++n) {
            const auto next = static_cast<unsigned char>(value[pos++]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}
std::uint16_t KeyCode(std::string_view name) {
    auto it = KeyNames().find(Lower(name));
    return it == KeyNames().end() ? 0 : it->second;
}
Settings ParseSettings(std::string_view text) {
    if (text.size() > 65536) Error(0,"file","","file exceeds 64 KiB");
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    if (!ValidUtf8(text)) Error(0,"file","","expected valid UTF-8 without control characters");
    Settings result;
    constexpr std::array<std::string_view,16> defaults{
        "Up","Down","Left","Right","Z","X","C","V","Enter","Backspace","A","S","Q","W","none","none"};
    for (std::size_t n = 0; n < defaults.size(); ++n) ParseBinding(result.keyboard[n],defaults[n],0,kButtons[n]);
    std::string section;
    std::set<std::string> seen;
    std::size_t line{};
    while (!text.empty()) {
        ++line;
        const auto end = text.find('\n');
        auto row = Trim(text.substr(0,end));
        text = end == text.npos ? std::string_view{} : text.substr(end+1);
        if (row.empty() || row.front() == ';') continue;
        if (row.front() == '[' && row.back() == ']') {
            section = Lower(Trim(row.substr(1,row.size()-2)));
            if (section != "display" && section != "keyboard" && section != "gamepad" && section != "audio" &&
                section != "logging" && section != "community") Error(line,section,"","unknown section");
            continue;
        }
        const auto equal = row.find('=');
        if (equal == row.npos || section.empty()) Error(line,section,"","expected a section and key = value");
        const auto key = Lower(Trim(row.substr(0,equal)));
        const auto value = Trim(row.substr(equal+1));
        if (!seen.insert(section + "." + key).second) Error(line,section,key,"duplicate key");
        auto boolean = [&] {
            const auto token = Lower(value);
            if (token != "true" && token != "false") Error(line,section,key,"expected true or false");
            return token == "true";
        };
        auto number = [&](bool exclusiveOne) {
            double output{};
            const auto parsed = std::from_chars(value.data(),value.data()+value.size(),output);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() || !std::isfinite(output) ||
                output < 0 || (exclusiveOne ? output >= 1 : output > 1))
                Error(line,section,key,exclusiveOne ? "expected a finite number in [0, 1)" : "expected a finite number in [0, 1]");
            return output;
        };
        if (section == "keyboard") {
            const auto button = std::find(kButtons.begin(),kButtons.end(),key);
            if (button == kButtons.end()) Error(line,section,key,"unknown Xbox button");
            ParseBinding(result.keyboard[button-kButtons.begin()],value,line,key);
        } else if (section == "display" && key == "fullscreen") result.fullscreen = boolean();
        else if (section == "display" && key == "vsync") result.vsync = boolean();
        else if (section == "gamepad" && key == "enabled") result.gamepad_enabled = boolean();
        else if (section == "gamepad" && key == "deadzone") result.deadzone = number(true);
        else if (section == "audio" && key == "volume") result.volume = number(false);
        else if (section == "logging" && key == "level") {
            result.log_level = Lower(value);
            if (result.log_level != "info" && result.log_level != "warn" && result.log_level != "error" &&
                result.log_level != "debug" && result.log_level != "trace" && result.log_level != "critical")
                Error(line,section,key,"expected info, warn, error, debug, trace or critical");
        } else if (section == "community" && key == "enabled") result.community_enabled = boolean();
        else if (section == "community" && key == "endpoint") {
            result.endpoint = value;
            if (!value.empty() && ((!value.starts_with("https://") && !value.starts_with("http://")) ||
                value.find_first_of(" \t") != value.npos)) Error(line,section,key,"expected an HTTP(S) URL or an empty value");
        } else if (section == "community" && key == "nickname") {
            result.nickname = value;
            if (value.empty() || value.size() > 64 || value.find_first_of("\t") != value.npos)
                Error(line,section,key,"use 1..64 UTF-8 bytes without tabs");
        } else Error(line,section,key,"unknown key");
    }
    std::map<std::uint16_t,std::size_t> occupied;
    for (std::size_t n=0; n<kButtons.size(); ++n) for (auto code : result.keyboard[n].keys) {
        auto [previous, inserted] = occupied.emplace(code,n);
        if (!inserted) {
            const auto changed = result.keyboard[n].line ? n : previous->second;
            const auto other = changed == n ? previous->second : n;
            Error(result.keyboard[changed].line,"keyboard",kButtons[changed],"binding conflicts with " + std::string(kButtons[other]));
        }
    }
    return result;
}
std::string ReadSettingsText(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open config.ini");
    std::string text(65537,'\0');
    input.read(text.data(),text.size());
    text.resize(input.gcount());
    if (input.bad()) throw std::runtime_error("Cannot read config.ini");
    if (text.size() > 65536) Error(0,"file","","file exceeds 64 KiB");
    return text;
}
Settings LoadSettings(const std::filesystem::path& path) {return ParseSettings(ReadSettingsText(path));}
}
