#pragma once
#include "common/settings.h"
#include "launcher/input_devices.h"
#include <chrono>
#include <optional>

namespace cave::launcher {
using ExitButtons=std::vector<std::string_view>;
struct GameExitShortcuts {
    std::uint16_t keyboard{VK_ESCAPE};
    ExitButtons buttons{"start","dpup"};
    std::map<std::string,controls::PadMap> controller_mappings;
};

inline std::string ExitBindingToken(std::string_view value) {
    const auto first=value.find_first_not_of(" \t");
    if(first==value.npos)return {};
    auto token=std::string(value.substr(first,value.find_last_not_of(" \t")-first+1));
    for(char& ch:token)if(ch>='A' && ch<='Z')ch+='a'-'A';
    return token;
}

inline std::optional<std::uint16_t> ParseExitKey(std::string_view value) {
    const auto name=ExitBindingToken(value);
    if(name=="none")return 0;
    if(name=="esc")return VK_ESCAPE;
    if(const auto key=KeyCode(name))return key;
    return {};
}

inline std::optional<ExitButtons> ParseExitButtons(std::string_view value) {
    ExitButtons buttons;
    if(ExitBindingToken(value)=="none")return buttons;
    for(;;) {
        const auto plus=value.find('+');
        const auto name=ExitBindingToken(value.substr(0,plus));
        const auto key=std::ranges::find(kButtons,name);
        if(key==kButtons.end())return {};
        const auto button=controls::kPadKeys[key-kButtons.begin()];
        if(std::ranges::find(buttons,button)!=buttons.end())return {};
        buttons.push_back(button);
        if(plus==value.npos)break;
        value.remove_prefix(plus+1);
    }
    if(buttons.size()<2)return {};
    return buttons;
}

class HoldToExit {
public:
    using Clock=std::chrono::steady_clock;

    void Reset() {
        wait_for_release_=true;
        pressed_since_.reset();
    }
    bool Poll(bool pressed,Clock::time_point now) {
        if(!pressed) {
            wait_for_release_=false;
            pressed_since_.reset();
            return false;
        }
        if(wait_for_release_)return false;
        if(!pressed_since_)pressed_since_=now;
        if(now-*pressed_since_<std::chrono::seconds(1))return false;
        Reset();
        return true;
    }
private:
    bool wait_for_release_{true};
    std::optional<Clock::time_point> pressed_since_;
};
}
