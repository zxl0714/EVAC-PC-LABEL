#pragma once
#include "host_input.h"
#include "launcher/game_exit.h"
#include <chrono>

namespace cave::ps2 {
enum MenuKey : unsigned { Toggle=1, Up=2, Down=4, Left=8, Right=16, Accept=32, Back=64 };

// Sample physical inputs independently of the guest pad reads. Games can stop
// reading their pad during loading, death animations or the paused menu itself.
class StateMenuInput {
    using Clock = std::chrono::steady_clock;
    struct Device {
        SDL_Joystick* stick{};
        launcher::ExitPadBinding binding;
    };
    std::map<SDL_JoystickID,Device> devices;
    std::map<std::string,launcher::ExitPadBinding> bindings;
    launcher::ExitKeys keys{launcher::kStateMenuDefaultKey};
    Clock::time_point nextPoll{}, nextScan{}, repeatAt{};
    HostPadSource& gamePad;
    bool circleConfirm, wasNavigating{};
    unsigned previous{}, repeatKey{}, ignoreHeld{};
    bool controllers{}, focused{}, keyboardEvents{};
    std::array<bool,256> keyboard{};
    unsigned keyEdges{};
    unsigned KeyboardShortcut() const {
        const auto key=[&](unsigned k) {
            return k && k<keyboard.size() && (keyboardEvents?keyboard[k]:(GetAsyncKeyState(k)&0x8000)!=0);
        };
        return key(keys[0]) && (!keys[1] || key(keys[1]))?Toggle:0;
    }
    unsigned Actions(uint16_t buttons) const {
        unsigned actions{};
        if (buttons&0x10) actions|=Up;
        if (buttons&0x40) actions|=Down;
        if (buttons&0x80) actions|=Left;
        if (buttons&0x20) actions|=Right;
        if (buttons&0x2000) actions|=circleConfirm?Accept:Back;
        if (buttons&0x4000) actions|=circleConfirm?Back:Accept;
        return actions;
    }
    void Refresh() {
        for (auto it=devices.begin(); it!=devices.end();) {
            if (SDL_JoystickConnected(it->second.stick)) { ++it; continue; }
            SDL_CloseJoystick(it->second.stick);
            it=devices.erase(it);
        }
        int count{};
        auto* ids=SDL_GetJoysticks(&count);
        for (int i=0; i<count; ++i) {
            const auto id=ids[i];
            if (devices.contains(id)) continue;
            char guid[33]{};
            SDL_GUIDToString(SDL_GetJoystickGUIDForID(id),guid,sizeof(guid));
            const auto it=bindings.find(guid);
            if (it==bindings.end()) continue;
            if (auto* stick=SDL_OpenJoystick(id)) devices.emplace(id,Device{stick,it->second});
        }
        SDL_free(ids);
    }
    static bool Source(SDL_Joystick* stick,std::string_view name) {
        if (name.empty()) return false;
        const bool minus=name.front()=='-';
        if (minus || name.front()=='+') name.remove_prefix(1);
        const char type=name.front();
        int index{};
        const auto end=name.data()+name.size();
        const auto result=std::from_chars(name.data()+1,end,index);
        if (result.ec!=std::errc{} || index<0) return false;
        if (type=='b') return SDL_GetJoystickButton(stick,index);
        if (type=='a') return (minus?-1:1)*SDL_GetJoystickAxis(stick,index)>15000;
        return type=='h' && end-result.ptr==2 && (SDL_GetJoystickHat(stick,index)&(result.ptr[1]-'0'));
    }
public:
    StateMenuInput(const std::filesystem::path& config,bool usePad,bool useKeyEvents,
                   HostPadSource& pad,bool confirmWithCircle)
        :gamePad(pad),circleConfirm(confirmWithCircle),keyboardEvents(useKeyEvents) {
        if (!config.empty() && std::filesystem::exists(config)) {
            const auto ini=Ini::Load(config);
            if (auto* v=ini.Get("collection",launcher::kStateMenuKey))
                keys=launcher::ParseExitKeys(v->text).value_or(keys);
            for (auto& [address,v]:ini.Values()) if (address.first==launcher::kStateMenuController) {
                if (auto b=launcher::ParseExitPadBinding(v.text); b && !b->front().empty()) bindings.emplace(address.second,*b);
            }
        }
        controllers=usePad && !bindings.empty() && SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    }
    ~StateMenuInput() {
        for (auto& [id,d]:devices) SDL_CloseJoystick(d.stick);
        if (controllers) SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }
    bool Held(unsigned actions) const { return (previous&actions)!=0; }
    void Key(unsigned code,bool down,uint16_t buttons,bool navigate) {
        if (!code) {
            keyboard.fill(false);
            previous=repeatKey=keyEdges=ignoreHeld=0;
            wasNavigating=false;
            return;
        }
        if (code>=keyboard.size() || keyboard[code]==down || !keyboardEvents) return;
        const auto before=KeyboardShortcut();
        keyboard[code]=down;
        const auto shortcut=KeyboardShortcut();
        keyEdges|=shortcut&~before;
        if (navigate && down && !shortcut) keyEdges|=Actions(buttons);
    }
    unsigned Poll(bool focus,bool navigate) {
        if (!focus) { focused=wasNavigating=false; previous=repeatKey=keyEdges=ignoreHeld=0; keyboard.fill(false); return 0; }
        const auto now=Clock::now();
        if (now<nextPoll) return 0;
        nextPoll=now+std::chrono::milliseconds(8);
        unsigned held=KeyboardShortcut();
        if (controllers) {
            if (now>=nextScan) { Refresh(); nextScan=now+std::chrono::seconds(1); }
            if (!devices.empty()) SDL_UpdateJoysticks();
            for (const auto& [id,d]:devices) {
                if (Source(d.stick,d.binding[0]) && (d.binding[1].empty() || Source(d.stick,d.binding[1]))) held|=Toggle;
            }
        }
        // Only read the game's mapped ports after the EE has parked. Sampling a
        // guest report during play would consume input intended for the game.
        if (navigate) {
            unsigned navigation{};
            for (unsigned port=0; port<2; ++port) {
                const auto sample=gamePad.Read(port);
                navigation|=Actions(sample.buttons);
                if (sample.axes[2]<64) navigation|=Left;
                if (sample.axes[2]>192) navigation|=Right;
                if (sample.axes[3]<64) navigation|=Up;
                if (sample.axes[3]>192) navigation|=Down;
            }
            if (!wasNavigating || (held&Toggle)) ignoreHeld=navigation;
            ignoreHeld&=navigation;
            held|=navigation&~ignoreHeld;
        }
        wasNavigating=navigate;
        unsigned edges=(focused?held&~previous:0)|std::exchange(keyEdges,0u);
        focused=true;
        previous=held;
        const unsigned direction=held&(Up|Down|Left|Right);
        if (repeatKey!=direction) { repeatKey=direction; repeatAt=now+std::chrono::milliseconds(400); }
        else if (direction && now>=repeatAt) { edges|=direction; repeatAt=now+std::chrono::milliseconds(130); }
        return edges;
    }
};
}
