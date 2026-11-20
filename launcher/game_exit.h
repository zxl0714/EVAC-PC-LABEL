#pragma once
#include "common/settings.h"
#include "launcher/input_devices.h"
#include <optional>

namespace cave::launcher {
inline constexpr std::string_view kExitKeySetting="game_exit_key";
inline constexpr std::string_view kPreviousExitKeySetting="big_picture_exit_key";
inline constexpr std::string_view kExitButtonsSetting="big_picture_exit_buttons";
inline constexpr std::string_view kExitControllerSection="game_exit_controller";
using ExitButtons=std::vector<std::string_view>;
using PadExitChord=std::array<std::string,2>;
struct GameExitShortcuts {
    std::uint16_t keyboard{VK_ESCAPE};
    ExitButtons buttons{"start","dpup"};
    std::map<std::string,controls::PadMap> controller_mappings;
    std::map<std::string,PadExitChord> controller_chords;
};

inline const IniValue* ExitKeySetting(const Ini& ini) {
    if(const auto* entry=ini.Get("collection",kExitKeySetting))return entry;
    return ini.Get("collection",kPreviousExitKeySetting);
}

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

inline bool ExitPadSource(std::string_view token) {
    if(token.size()<2)return false;
    const bool signed_axis=token.front()=='-' || token.front()=='+';
    if(signed_axis)token.remove_prefix(1);
    const auto kind=token.front();
    if(signed_axis && kind!='a')return false;
    token.remove_prefix(1);
    unsigned index{};
    const auto end=token.data()+token.size();
    const auto result=std::from_chars(token.data(),end,index);
    if(result.ec!=std::errc{})return false;
    if(kind=='b')return index<256 && result.ptr==end;
    if(kind=='a')return signed_axis && index<64 && result.ptr==end;
    return kind=='h' && index<32 && end-result.ptr==2 && *result.ptr=='.' &&
        std::string_view("1248").find(result.ptr[1])!=std::string_view::npos;
}

inline std::optional<PadExitChord> ParseExitChord(std::string_view value) {
    if(ExitBindingToken(value)=="none")return PadExitChord{};
    const auto comma=value.find(',');
    if(comma==value.npos)return {};
    PadExitChord chord{ExitBindingToken(value.substr(0,comma)),ExitBindingToken(value.substr(comma+1))};
    if(chord[0]==chord[1] || !std::ranges::all_of(chord,ExitPadSource))return {};
    return chord;
}

inline std::optional<PadExitChord> CaptureExitChord(const controls::PadSnapshot& baseline,
                                                  const controls::PadSnapshot& state) {
    PadExitChord chord;
    std::size_t count{};
    const auto add=[&](std::string source) {
        if(count<chord.size())chord[count]=std::move(source);
        ++count;
    };
    for(std::size_t i=0;i<std::min(baseline.buttons.size(),state.buttons.size());++i)
        if(state.buttons[i] && !baseline.buttons[i])add("b"+std::to_string(i));
    for(std::size_t i=0;i<std::min(baseline.hats.size(),state.hats.size());++i) {
        const auto direction=state.hats[i];
        if((direction==1 || direction==2 || direction==4 || direction==8) && !(baseline.hats[i]&direction))
            add("h"+std::to_string(i)+'.'+std::to_string(direction));
    }
    for(std::size_t i=0;i<std::min(baseline.axes.size(),state.axes.size());++i) {
        const auto value=state.axes[i];
        if(std::abs(value)>15000 && std::abs(value-baseline.axes[i])>15000)
            add(std::string(value>0?"+a":"-a")+std::to_string(i));
    }
    if(count==chord.size())return chord;
    return {};
}
}
