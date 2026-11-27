#pragma once
#include "common/settings.h"
#include "launcher/input_devices.h"
#include <optional>

namespace cave::launcher {
inline constexpr std::string_view kExitKeySetting="game_exit_key";
inline constexpr std::string_view kPreviousExitKeySetting="big_picture_exit_key";
inline constexpr std::string_view kExitButtonsSetting="big_picture_exit_buttons";
inline constexpr std::string_view kExitControllerSection="game_exit_controller";
inline constexpr std::string_view kStateMenuKey="ps2_state_menu_key";
inline constexpr std::string_view kStateMenuController="ps2_state_menu_controller";
inline constexpr std::uint16_t kStateMenuDefaultKey=VK_F5;
using ExitButtons=std::vector<std::string_view>;
using ExitKeys=std::array<std::uint16_t,2>;
using ExitPadBinding=std::array<std::string,2>;
struct GameExitShortcuts {
    ExitKeys keyboard{VK_ESCAPE};
    ExitButtons buttons{"start","dpup"};
    bool default_start_modifier{true};
    std::map<std::string,controls::PadMap> controller_mappings;
    std::map<std::string,ExitPadBinding> controller_bindings;
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

inline std::optional<ExitKeys> ParseExitKeys(std::string_view value) {
    if(ExitBindingToken(value)=="none")return ExitKeys{};
    const auto code=[](std::string_view part) {
        const auto name=ExitBindingToken(part);
        return name=="esc"?std::uint16_t(VK_ESCAPE):KeyCode(name);
    };
    const auto plus=value.find('+');
    const auto first=code(value.substr(0,plus));
    if(!first)return {};
    if(plus==value.npos)return ExitKeys{first};
    const auto second=code(value.substr(plus+1));
    if(!second || first==second)return {};
    return ExitKeys{first,second};
}

inline std::string ExitKeyText(const ExitKeys& keys) {
    if(!keys[0])return "none";
    const auto name=KeyName(keys[0]);
    return keys[1]?name+" + "+KeyName(keys[1]):name;
}

inline std::uint16_t ExitVirtualKey(std::uint16_t key) {
    switch(key) {
    case VK_LSHIFT:case VK_RSHIFT:return VK_SHIFT;
    case VK_LCONTROL:case VK_RCONTROL:return VK_CONTROL;
    case VK_LMENU:case VK_RMENU:return VK_MENU;
    default:return key;
    }
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

inline std::optional<ExitPadBinding> ParseExitPadBinding(std::string_view value) {
    if(ExitBindingToken(value)=="none")return ExitPadBinding{};
    const auto comma=value.find(',');
    auto first=ExitBindingToken(value.substr(0,comma));
    if(!ExitPadSource(first))return {};
    if(comma==value.npos)return ExitPadBinding{std::move(first)};
    auto second=ExitBindingToken(value.substr(comma+1));
    if(first==second || !ExitPadSource(second))return {};
    return ExitPadBinding{std::move(first),std::move(second)};
}

template<class Input>
std::optional<std::array<Input,2>> RecordExitBinding(Input& first,std::span<const Input> pressed) {
    if(first!=Input{} && std::ranges::find(pressed,first)==pressed.end())return std::array<Input,2>{first};
    if(pressed.empty())return {};
    if(first==Input{})first=pressed.front();
    if(const auto second=std::ranges::find_if(pressed,[&](const auto& key){return key!=first;});second!=pressed.end())
        return std::array<Input,2>{first,*second};
    return {};
}

inline std::vector<std::string> PressedExitPadInputs(const controls::PadSnapshot& baseline,
                                                     const controls::PadSnapshot& state) {
    std::vector<std::string> pressed;
    for(std::size_t i=0;i<std::min(baseline.buttons.size(),state.buttons.size());++i)
        if(state.buttons[i] && !baseline.buttons[i])pressed.push_back("b"+std::to_string(i));
    for(std::size_t i=0;i<std::min(baseline.hats.size(),state.hats.size());++i) {
        const auto direction=state.hats[i];
        if((direction==1 || direction==2 || direction==4 || direction==8) && !(baseline.hats[i]&direction))
            pressed.push_back("h"+std::to_string(i)+'.'+std::to_string(direction));
    }
    for(std::size_t i=0;i<std::min(baseline.axes.size(),state.axes.size());++i) {
        const auto value=state.axes[i];
        if(std::abs(value)>15000 && std::abs(value-baseline.axes[i])>15000)
            pressed.push_back(std::string(value>0?"+a":"-a")+std::to_string(i));
    }
    return pressed;
}
}
