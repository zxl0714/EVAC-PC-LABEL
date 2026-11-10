#pragma once
#include "common/paths.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace cave::launcher::controls {
using PadMap=std::map<std::string,std::string>;

inline PadMap ParsePadMap(std::string_view text) {
    PadMap map;
    while(!text.empty()) {
        const auto comma=text.find(',');
        const auto entry=text.substr(0,comma);
        const auto colon=entry.find(':');
        if(colon!=entry.npos && colon && colon+1<entry.size()) {
            const auto name=entry.substr(0,colon);
            if(name!="platform" && name!="crc" && name!="hint" &&
               entry.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ,.:_+-~")==entry.npos)
                map[std::string(name)]=entry.substr(colon+1);
        }
        if(comma==text.npos)break;
        text.remove_prefix(comma+1);
    }
    return map;
}

inline std::string SerializePadMap(const PadMap& map) {
    std::string value;
    for(const auto& [name,source]:map)value+=name+':'+source+',';
    // An explicit empty mapping must not be confused with restoring SDL defaults.
    return value.empty()?",":value;
}

struct PadSnapshot {
    std::vector<int> buttons,axes,hats;
};

inline std::string CapturePad(const PadSnapshot& previous,const PadSnapshot& now,
                              const PadSnapshot& baseline,bool analog,bool trigger) {
    if(!analog) {
        for(std::size_t i=0;i<now.buttons.size() && i<previous.buttons.size();++i)
            if(now.buttons[i] && !previous.buttons[i])return "b"+std::to_string(i);
        for(std::size_t i=0;i<now.hats.size() && i<previous.hats.size();++i) {
            const auto added=now.hats[i]&~previous.hats[i];
            for(const int direction:{1,2,4,8}) if(added==direction)
                return "h"+std::to_string(i)+'.'+std::to_string(direction);
        }
    }
    std::size_t axis=now.axes.size();
    int strongest=15000;
    for(std::size_t i=0;i<now.axes.size() && i<baseline.axes.size();++i) {
        const auto difference=std::abs(now.axes[i]-baseline.axes[i]);
        if(difference>strongest && std::abs(now.axes[i])>15000){axis=i;strongest=difference;}
    }
    if(axis==now.axes.size())return {};
    const bool positive=now.axes[axis]>baseline.axes[axis];
    const auto name="a"+std::to_string(axis);
    if(analog || (trigger && std::abs(baseline.axes[axis])>24576))return positive?name:name+'~';
    return (positive?"+":"-")+name;
}

inline bool PadSourceActive(std::string_view source,const PadSnapshot& state,bool trigger) {
    if(source.empty())return false;
    const auto sign=source.front()=='-'?-1:source.front()=='+'?1:0;
    if(sign)source.remove_prefix(1);
    if(source.size()<2)return false;
    const auto type=source.front();source.remove_prefix(1);
    unsigned index{};
    const auto parsed=std::from_chars(source.data(),source.data()+source.size(),index);
    if(parsed.ec!=std::errc{})return false;
    const std::string_view suffix(parsed.ptr,source.data()+source.size());
    if(type=='b')return index<state.buttons.size() && state.buttons[index];
    if(type=='h' && index<state.hats.size() && suffix.size()==2 && suffix.front()=='.')
        return (state.hats[index]&(suffix.back()-'0'))!=0;
    if(type!='a' || index>=state.axes.size())return false;
    const auto value=state.axes[index]*(suffix=="~"?-1:1);
    if(sign)return value*sign>15000;
    return trigger?value>15000:std::abs(value)>15000;
}

class InputDevices {
public:
    explicit InputDevices(const fs::path& root) {
        module_=LoadLibraryExW(DataChild(root,"runtime/ps2/SDL3.dll").c_str(),nullptr,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module_)throw std::runtime_error("Cannot load SDL3 for controller settings");
        try {
#define EVAC_LOAD_SDL(name) name=Load<decltype(name)>("SDL_" #name)
            EVAC_LOAD_SDL(InitSubSystem);EVAC_LOAD_SDL(QuitSubSystem);EVAC_LOAD_SDL(SetMainReady);
            EVAC_LOAD_SDL(SetHint);EVAC_LOAD_SDL(GetJoysticks);EVAC_LOAD_SDL(GetJoystickNameForID);
            EVAC_LOAD_SDL(GetJoystickGUIDForID);EVAC_LOAD_SDL(GUIDToString);EVAC_LOAD_SDL(OpenJoystick);
            EVAC_LOAD_SDL(CloseJoystick);EVAC_LOAD_SDL(JoystickConnected);EVAC_LOAD_SDL(UpdateJoysticks);
            EVAC_LOAD_SDL(FlushEvents);EVAC_LOAD_SDL(GetNumJoystickAxes);EVAC_LOAD_SDL(GetNumJoystickButtons);
            EVAC_LOAD_SDL(GetNumJoystickHats);EVAC_LOAD_SDL(GetJoystickAxis);EVAC_LOAD_SDL(GetJoystickButton);
            EVAC_LOAD_SDL(GetJoystickHat);EVAC_LOAD_SDL(GetGamepadMappingForID);EVAC_LOAD_SDL(free);
#undef EVAC_LOAD_SDL
            SetMainReady();
            SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
            if(!InitSubSystem(SDL_INIT_GAMEPAD))throw std::runtime_error("Cannot initialize SDL3 controller settings");
        }catch(...) {FreeLibrary(module_);throw;}
    }
    ~InputDevices() {
        if(joystick_)CloseJoystick(joystick_);
        QuitSubSystem(SDL_INIT_GAMEPAD);
        FreeLibrary(module_);
    }
    InputDevices(const InputDevices&)=delete;
    InputDevices& operator=(const InputDevices&)=delete;
    struct Device {SDL_JoystickID id{};std::string name,guid;bool operator==(const Device&)const=default;};
    void Update() {UpdateJoysticks();FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);}
    std::vector<Device> List() {
        int count{};
        auto ids=std::unique_ptr<SDL_JoystickID,decltype(free)>(GetJoysticks(&count),free);
        std::vector<Device> result;
        for(int i=0;ids && i<count;++i) {
            const auto id=ids.get()[i];
            char guid[33]{};GUIDToString(GetJoystickGUIDForID(id),guid,sizeof(guid));
            if(const auto* name=GetJoystickNameForID(id))result.push_back({id,name,guid});
            else result.push_back({id,"Controller",guid});
        }
        return result;
    }
    bool Select(SDL_JoystickID id) {
        if(joystick_)CloseJoystick(joystick_);
        joystick_=id?OpenJoystick(id):nullptr;
        return joystick_!=nullptr;
    }
    PadMap Defaults(SDL_JoystickID id) {
        auto mapping=std::unique_ptr<char,decltype(free)>(GetGamepadMappingForID(id),free);
        if(!mapping)return {};
        std::string_view text(mapping.get());
        const auto first=text.find(',');if(first==text.npos)return {};
        const auto second=text.find(',',first+1);if(second==text.npos)return {};
        return ParsePadMap(text.substr(second+1));
    }
    PadSnapshot Read() {
        PadSnapshot result;
        if(!joystick_ || !JoystickConnected(joystick_))return result;
        for(int i=0,n=std::min(256,GetNumJoystickButtons(joystick_));i<n;++i)result.buttons.push_back(GetJoystickButton(joystick_,i));
        for(int i=0,n=std::min(64,GetNumJoystickAxes(joystick_));i<n;++i)result.axes.push_back(GetJoystickAxis(joystick_,i));
        for(int i=0,n=std::min(32,GetNumJoystickHats(joystick_));i<n;++i)result.hats.push_back(GetJoystickHat(joystick_,i));
        return result;
    }
private:
    template<class Fn> Fn Load(const char* name) {
        const auto address=GetProcAddress(module_,name);
        if(!address)throw std::runtime_error(std::string("SDL3 controller API is unavailable: ")+name);
        return reinterpret_cast<Fn>(address);
    }
    HMODULE module_{};
    SDL_Joystick* joystick_{};
#define EVAC_SDL_MEMBER(name) decltype(&SDL_##name) name{}
    EVAC_SDL_MEMBER(InitSubSystem);EVAC_SDL_MEMBER(QuitSubSystem);EVAC_SDL_MEMBER(SetMainReady);
    EVAC_SDL_MEMBER(SetHint);EVAC_SDL_MEMBER(GetJoysticks);EVAC_SDL_MEMBER(GetJoystickNameForID);
    EVAC_SDL_MEMBER(GetJoystickGUIDForID);EVAC_SDL_MEMBER(GUIDToString);EVAC_SDL_MEMBER(OpenJoystick);
    EVAC_SDL_MEMBER(CloseJoystick);EVAC_SDL_MEMBER(JoystickConnected);EVAC_SDL_MEMBER(UpdateJoysticks);
    EVAC_SDL_MEMBER(FlushEvents);EVAC_SDL_MEMBER(GetNumJoystickAxes);EVAC_SDL_MEMBER(GetNumJoystickButtons);
    EVAC_SDL_MEMBER(GetNumJoystickHats);EVAC_SDL_MEMBER(GetJoystickAxis);EVAC_SDL_MEMBER(GetJoystickButton);
    EVAC_SDL_MEMBER(GetJoystickHat);EVAC_SDL_MEMBER(GetGamepadMappingForID);EVAC_SDL_MEMBER(free);
#undef EVAC_SDL_MEMBER
};
}
