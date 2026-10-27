#include "common/settings.h"
#include "common/config_update.h"
#include "common/paths.h"
#include "default_config.h"
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cave {
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
void ParseBinding(Binding& binding, std::string_view value, std::size_t line, std::string_view button, std::string_view section) {
    binding = Binding{{}, std::string(value), line, std::string(section)};
    if (Lower(value) == "none") return;
    if (value.empty()) Error(line,section,button,"use none to disable a binding");
    std::set<std::uint16_t> seen;
    while (true) {
        const auto comma = value.find(',');
        const auto token = Trim(value.substr(0, comma));
        const auto code = KeyCode(token);
        if (!code) Error(line,section,button,"unknown key: " + std::string(token));
        if (code == 114 || code == 115 || code == 118 || code == 119 || code == 122 || code == 192)
            Error(line,section,button,"key is reserved for host diagnostics or fullscreen");
        if (!seen.insert(code).second) Error(line,section,button,"duplicate key or key alias");
        binding.keys.push_back(code);
        if (binding.keys.size() > 8) Error(line,section,button,"at most eight keys per button");
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

static Settings ResolveSettingsImpl(const Ini& ini, std::string_view game_id) {
    Settings result;
    constexpr std::array<std::string_view,16> defaults{
        "Up","Down","Left","Right","Z","X","C","V","Enter","Backspace","A","S","Q","W","none","none"};
    for (std::size_t n=0; n<defaults.size(); ++n)
        ParseBinding(result.keyboard[n],defaults[n],0,kButtons[n],"keyboard");
    constexpr std::array<std::string_view,4> right_stick_defaults{"I","K","J","L"};
    for (std::size_t n=0; n<right_stick_defaults.size(); ++n)
        ParseBinding(result.keyboard_right_stick[n],game_id=="guwange"?right_stick_defaults[n]:"none",
                     0,kRightStickButtons[n],"game.guwange.keyboard");
    auto apply = [&](std::string_view section, std::string_view category, std::string_view key, const IniValue& entry) {
        const auto value=std::string_view(entry.text);
        const auto line=entry.line;
        auto boolean = [&] {
            const auto token=Lower(value);
            if (token!="true" && token!="false") Error(line,section,key,"expected true or false");
            return token=="true";
        };
        auto number = [&](bool exclusiveOne) {
            double output{};
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),output);
            if (parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || !std::isfinite(output) ||
                output<0 || (exclusiveOne ? output>=1 : output>1))
                Error(line,section,key,exclusiveOne ? "expected a finite number in [0, 1)" : "expected a finite number in [0, 1]");
            return output;
        };
        if (category=="keyboard") {
            const auto button=std::find(kButtons.begin(),kButtons.end(),key);
            if (button!=kButtons.end())
                ParseBinding(result.keyboard[button-kButtons.begin()],value,line,key,section);
            else {
                const auto direction=std::find(kRightStickButtons.begin(),kRightStickButtons.end(),key);
                if (game_id!="guwange" || section!="game.guwange.keyboard" || direction==kRightStickButtons.end())
                    Error(line,section,key,"unknown controller button");
                ParseBinding(result.keyboard_right_stick[direction-kRightStickButtons.begin()],value,line,key,section);
            }
        } else if (category=="display" && key=="fullscreen") result.fullscreen=boolean();
        else if (category=="display" && key=="vsync") result.vsync=boolean();
        else if (category=="gamepad" && key=="enabled") result.gamepad_enabled=boolean();
        else if (category=="gamepad" && key=="deadzone") result.deadzone=number(true);
        else if (category=="audio" && key=="volume") result.volume=number(false);
        else if (category=="logging" && key=="level") {
            result.log_level=Lower(value);
            if (result.log_level!="info" && result.log_level!="warn" && result.log_level!="error" &&
                result.log_level!="debug" && result.log_level!="trace" && result.log_level!="critical")
                Error(line,section,key,"expected info, warn, error, debug, trace or critical");
        } else if (category=="community" && key=="enabled") result.community_enabled=boolean();
        else if (category=="community" && key=="endpoint") {
            result.endpoint=value;
            if (value.empty() || ((!value.starts_with("https://") && !value.starts_with("http://")) ||
                value.find_first_of(" \t\r\n")!=value.npos))
                Error(line,section,key,"expected an HTTP(S) API root");
        } else if (category=="community" && key=="nickname") {
            result.nickname=value;
            if (value.empty() || value.size()>64 || !ValidUtf8(value) || value.find('\t')!=value.npos)
                Error(line,section,key,"use 1..64 UTF-8 bytes without control characters");
        } else Error(line,section,key,"unknown key");
    };
    constexpr std::array<std::string_view,6> categories{"display","audio","gamepad","keyboard","community","logging"};
    // Validate public values first; an override does not hide malformed public
    // configuration. Other games' values are not interpreted by this launch.
    for (bool override : {false,true}) for (auto category : categories) {
        if (override && game_id.empty()) continue;
        const auto section=override ? "game."+std::string(game_id)+"."+std::string(category) : std::string(category);
        for (const auto& [address,entry] : ini.Values())
            if (address.first==section) apply(section,category,address.second,entry);
    }
    if (game_id == "sdoj") if (const auto* entry = ini.Get("game.sdoj", "frame_delay")) {
        const auto& value = entry->text;
        const auto parsed = std::from_chars(value.data(), value.data()+value.size(), result.sdoj_frame_delay);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() ||
            result.sdoj_frame_delay < 1 || result.sdoj_frame_delay > 3)
            Error(entry->line, "game.sdoj", "frame_delay", "expected 3 (original), 2 (balanced) or 1 (low latency)");
    }
    if (game_id == "espgaluda2") if (const auto* entry = ini.Get("game.espgaluda2", "frame_delay")) {
        const auto& value = entry->text;
        const auto parsed = std::from_chars(value.data(), value.data()+value.size(), result.g2_frame_delay);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() ||
            result.g2_frame_delay < 1 || result.g2_frame_delay > 2)
            Error(entry->line, "game.espgaluda2", "frame_delay", "expected 2 (original) or 1 (experimental low latency)");
    }
    std::map<std::uint16_t,std::pair<const Binding*,std::string_view>> occupied;
    auto check_binding=[&](const Binding& binding,std::string_view name) {
      for (auto code : binding.keys) {
        auto [previous,inserted]=occupied.emplace(code,std::pair{&binding,name});
        if (!inserted) {
            const auto& changed=binding.line?binding:*previous->second.first;
            const auto changed_name=binding.line?name:previous->second.second;
            const auto other=binding.line?previous->second.second:name;
            Error(changed.line,changed.section,changed_name,"binding conflicts with "+std::string(other));
        }
      }
    };
    for (std::size_t n=0; n<kButtons.size(); ++n) check_binding(result.keyboard[n],kButtons[n]);
    for (std::size_t n=0; n<kRightStickButtons.size(); ++n)
        check_binding(result.keyboard_right_stick[n],kRightStickButtons[n]);
    return result;
}
Settings ResolveSettings(const Ini& ini, std::string_view game_id) {
    (void)FindGame(game_id);
    return ResolveSettingsImpl(ini,game_id);
}
Settings ResolveSharedSettings(const Ini& ini) { return ResolveSettingsImpl(ini,{}); }
Settings PreviewSharedPreferences(std::string_view before, bool fullscreen, std::string_view nickname) {
    const std::array edits{IniEdit{"display","fullscreen",fullscreen?"true":"false"},
                           IniEdit{"community","nickname",std::string(nickname)}};
    const auto candidate=ResolveSharedSettings(Ini::Parse(EditIni(before,edits)));
    if(candidate.nickname!=nickname)
        throw std::runtime_error("Nickname must not start or end with whitespace.");
    return candidate;
}
void SaveSharedPreferences(const fs::path& root, std::string_view before, bool fullscreen, std::string_view nickname) {
    (void)PreviewSharedPreferences(before,fullscreen,nickname);
    const std::array edits{IniEdit{"display","fullscreen",fullscreen?"true":"false"},
                           IniEdit{"community","nickname",std::string(nickname)}};
    UpdateConfig(root,before,edits);
}
Settings ParseSettings(std::string_view text, std::string_view game_id) {
    return ResolveSettings(Ini::Parse(text),game_id);
}
Settings LoadSettings(const fs::path& path, std::string_view game_id) {
    return ResolveSettings(Ini::Load(path),game_id);
}
std::string_view DefaultConfig() { return kDefaultConfig; }
namespace {
fs::path CheckedConfigPath(const fs::path& root,const fs::path& relative) {
    const auto literal=root/relative;
    const auto attributes=GetFileAttributesW(literal.c_str());
    if (attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("config.ini must be a local file, not a file link");
    const auto path=Resolve(root,relative);
    if (!Within(path,root)) throw std::runtime_error("config.ini must stay inside the collection");
    if (fs::exists(path) && !fs::is_regular_file(path))
        throw std::runtime_error("config.ini is not a regular file");
    return path;
}
}
fs::path ConfigPath(const fs::path& requested_root) {
    const auto root=fs::canonical(requested_root);
    const auto path=CheckedConfigPath(root,L"user-data/config.ini");
    if (fs::exists(path)) return path;
    const auto legacy=CheckedConfigPath(root,L"config.ini");
    return fs::exists(legacy)?legacy:path;
}
fs::path EnsureConfig(const fs::path& requested_root) {
    const auto root=fs::canonical(requested_root);
    const auto path=CheckedConfigPath(root,L"user-data/config.ini");
    if (fs::exists(path)) return path;
    const auto legacy=CheckedConfigPath(root,L"config.ini");
    fs::create_directories(path.parent_path());
    if (fs::exists(legacy)) {
        // Same-volume move publishes the exact original bytes without replacing
        // an existing new-format config. Relative data paths still use root.
        if (MoveFileExW(legacy.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)) return path;
        if (fs::is_regular_file(path)) return path;
        throw std::runtime_error("Cannot migrate config.ini to user-data/config.ini");
    }
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) {
        if (GetLastError()==ERROR_FILE_EXISTS && fs::is_regular_file(path)) return path;
        throw std::runtime_error("Cannot create default config.ini");
    }
    const auto text=DefaultConfig();
    DWORD written{};
    const bool okay=WriteFile(file,text.data(),DWORD(text.size()),&written,nullptr) && written==text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!okay) throw std::runtime_error("Cannot write default config.ini");
    return path;
}
}  // namespace cave
