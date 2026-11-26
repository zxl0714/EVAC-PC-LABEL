#include "sdl_gamepad.h"
#include "common/stick_deadzone.h"
#include "common/controller_ports.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

namespace kt::input {
namespace {
std::string Quoted(const char* value) {
    std::string out="\"";
    if (value) for (const unsigned char c : std::string(value).substr(0, 512)) {
        if (c=='"' || c=='\\') { out+='\\'; out+=static_cast<char>(c); }
        else if (c<32) { out+='?'; }
        else { out+=static_cast<char>(c); }
    }
    return out+'"';
}
std::uint32_t Mask(unsigned button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return 0x4000;
    case SDL_GAMEPAD_BUTTON_EAST: return 0x2000;
    case SDL_GAMEPAD_BUTTON_WEST: return 0x8000;
    case SDL_GAMEPAD_BUTTON_NORTH: return 0x1000;
    case SDL_GAMEPAD_BUTTON_BACK: case SDL_GAMEPAD_BUTTON_TOUCHPAD: return 0x100000;
    case SDL_GAMEPAD_BUTTON_START: return 8;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return 2;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return 4;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 0x400;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 0x800;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return 0x10;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 0x20;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 0x40;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return 0x80;
    default: return 0; // No host/system action for Guide or extra vendor buttons.
    }
}
std::uint8_t Axis(Sint16 value) { return static_cast<std::uint8_t>((int(value)+32768)/256); }
std::uint8_t Trigger(Sint16 value) { return static_cast<std::uint8_t>((std::max)(0,int(value))*255/32767); }
struct JoystickLock { JoystickLock() { SDL_LockJoysticks(); } ~JoystickLock() { SDL_UnlockJoysticks(); } };
void LoadMappings() {
    std::wstring filename(32768, L'\0');
    const DWORD length=GetModuleFileNameW(nullptr,filename.data(),static_cast<DWORD>(filename.size()));
    if (!length || length>=filename.size()) { throw std::runtime_error("Cannot resolve gamepad mapping location"); }
    filename.resize(length);
    const auto path=std::filesystem::path(filename).parent_path()/L"gamecontrollerdb.txt";
    if (!std::filesystem::exists(path)) { return; }
    if (std::filesystem::file_size(path)>1024*1024) { throw std::runtime_error("Gamepad mappings exceed 1 MiB"); }
    std::ifstream file(path,std::ios::binary);
    if (!file) { throw std::runtime_error("Cannot read gamecontrollerdb.txt"); }
    std::string line;
    unsigned number=0, added=0;
    while (std::getline(file,line)) {
        ++number;
        if (number==1 && line.starts_with("\xef\xbb\xbf")) line.erase(0,3);
        if (line.find('\0')!=std::string::npos || line.size()>4096) throw std::runtime_error("Invalid gamepad mapping line");
        const auto first=line.find_first_not_of(" \t\r");
        if (first==std::string::npos || line[first]=='#' || line[first]==';') continue;
        line=line.substr(first);
        if (SDL_AddGamepadMapping(line.c_str())<0)
            throw std::runtime_error("Gamepad mapping line "+std::to_string(number)+": "+SDL_GetError());
        ++added;
    }
    if (file.bad()) throw std::runtime_error("Cannot finish reading gamepad mappings");
    std::printf("{\"event\":\"native_sdl_gamepad_mappings\",\"entries\":%u,\"restart_required\":true}\n",added);
}
}
struct SdlGamepad::Impl {
    bool initialized{}, focused{};
    bool enabled = true;
    bool apply_deadzone{}, swap_players{};
    double deadzone{}; // Legacy diagnostic mode preserves unfiltered axes.
    struct Port {
        SDL_Gamepad* gamepad{};
        SDL_JoystickID device{};
        std::uint32_t pending{}, trigger_held{};
        std::array<std::uint64_t,32> edges{};
        void Press(std::uint32_t mask) {
            pending |= mask;
            for (unsigned bit=0;bit<edges.size();++bit) if (mask&(1u<<bit)) ++edges[bit];
        }
    };
    std::array<Port,2> ports{};
    std::array<SDL_JoystickID,2> test_devices{}; // Isolate authored virtual devices from attached hardware.
    Uint64 next_scan{};
    std::mutex mutex;
    static bool SDLCALL Watch(void* context, SDL_Event* event) {
        auto& self=*static_cast<Impl*>(context);
        const bool button=event->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const bool trigger=event->type==SDL_EVENT_GAMEPAD_AXIS_MOTION &&
            (event->gaxis.axis==SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event->gaxis.axis==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        if (!button && !trigger) return true;
        const auto id=button?event->gbutton.which:event->gaxis.which;
        std::lock_guard lock(self.mutex);
        for (auto& port:self.ports) {
            if (port.device!=id) continue;
            if (button) {
                if (self.focused) port.Press(Mask(event->gbutton.button));
            } else {
                const std::uint32_t bit=event->gaxis.axis==SDL_GAMEPAD_AXIS_LEFT_TRIGGER?0x100:0x200;
                if (Trigger(event->gaxis.value)>30) {
                    if (self.focused && !(port.trigger_held&bit)) port.Press(bit);
                    port.trigger_held|=bit;
                } else port.trigger_held&=~bit;
            }
        }
        return true;
    }
    void Select(unsigned index, SDL_Gamepad* selected, SDL_JoystickID id) {
        auto& port=ports[index];
        {
            std::lock_guard lock(mutex);
            port.device=id;
            port.pending=port.trigger_held=0;
            port.edges.fill(0);
        }
        if (port.gamepad) SDL_CloseGamepad(port.gamepad);
        port.gamepad=selected;
        if (selected) {
            char guid[33]{}; SDL_GUIDToString(SDL_GetGamepadGUIDForID(id),guid,sizeof(guid));
            std::printf("{\"event\":\"native_sdl_gamepad_connected\",\"player\":%u,\"device\":%u,\"name\":%s,\"guid\":\"%s\",\"vendor_id\":%u,\"product_id\":%u,\"type\":%u}\n",
                cave::ControllerPort(index,swap_players)+1,id,Quoted(SDL_GetGamepadName(selected)).c_str(),guid,SDL_GetGamepadVendor(selected),SDL_GetGamepadProduct(selected),unsigned(SDL_GetGamepadType(selected)));
            char* mapping=SDL_GetGamepadMapping(selected);
            std::printf("{\"event\":\"native_sdl_gamepad_mapping\",\"mapping\":%s}\n",Quoted(mapping).c_str());
            SDL_free(mapping);
        } else std::printf("{\"event\":\"native_sdl_gamepad_disconnected\",\"player\":%u}\n",cave::ControllerPort(index,swap_players)+1);
    }
    void Update();
    GamepadSnapshot Read(unsigned index);
    ~Impl() {
        if (!initialized) return;
        SDL_RemoveEventWatch(Watch,this);
        for (auto& port:ports) if (port.gamepad) SDL_CloseGamepad(port.gamepad);
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
};
SdlGamepad::SdlGamepad() : impl_(std::make_unique<Impl>()) {}
SdlGamepad::~SdlGamepad() = default;
void SdlGamepad::Configure(bool enabled, double deadzone, bool swap_players) {
    if (!std::isfinite(deadzone) || deadzone < 0 || deadzone >= 1) throw std::runtime_error("Invalid gamepad deadzone");
    if (impl_->initialized) throw std::runtime_error("Gamepad configuration requires a restart");
    impl_->enabled = enabled;
    impl_->swap_players = swap_players;
    impl_->deadzone = deadzone;
    impl_->apply_deadzone = true;
}
void SdlGamepad::Initialize() {
    if (!impl_->enabled) return;
    if (impl_->initialized) return;
    SDL_SetMainReady();
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_THREAD,"1",SDL_HINT_OVERRIDE);
    // SDL doesn't own our Win32 window. Our own focus gate is authoritative.
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1",SDL_HINT_OVERRIDE);
    if (!SDL_Init(SDL_INIT_GAMEPAD)) throw std::runtime_error(std::string("SDL gamepad initialization: ")+SDL_GetError());
    impl_->initialized=true;
    LoadMappings();
    if (!SDL_AddEventWatch(Impl::Watch,impl_.get())) throw std::runtime_error("Cannot install SDL gamepad event watch");
    std::printf("{\"event\":\"native_sdl_gamepad_initialized\",\"sdl_version\":%d,\"owns_window\":false,\"owns_audio\":false}\n",SDL_GetVersion());
}
void SdlGamepad::Focus(bool focused) {
    std::lock_guard lock(impl_->mutex);
    if (focused!=impl_->focused) for (auto& port:impl_->ports) port.pending=0;
    impl_->focused=focused;
}
void SdlGamepad::ClearPending(unsigned player) {
    std::lock_guard lock(impl_->mutex);
    impl_->ports.at(cave::ControllerPort(player,impl_->swap_players)).pending=0;
}
bool SdlGamepad::HasPending(unsigned player) {
    std::lock_guard lock(impl_->mutex);
    return impl_->ports.at(cave::ControllerPort(player,impl_->swap_players)).pending!=0;
}
void SdlGamepad::Impl::Update() {
    if (!initialized) return;
    SDL_UpdateGamepads();
    bool vacancy=false;
    for (unsigned i=0;i<ports.size();++i) {
        if (ports[i].gamepad && !SDL_GamepadConnected(ports[i].gamepad)) {
            Select(i,nullptr,0);
            next_scan=0;
        }
        vacancy |= !ports[i].gamepad;
    }
    if (vacancy && SDL_GetTicks()>=next_scan) {
        next_scan=SDL_GetTicks()+250;
        int count=0;
        auto* ids=SDL_GetGamepads(&count);
        for (int i=0;ids && i<count;++i) {
            const auto id=ids[i];
            if (test_devices[0] && std::ranges::find(test_devices,id)==test_devices.end()) continue;
            if (std::ranges::any_of(ports,[&](const Port& p){return p.device==id;})) continue;
            const int slot=cave::AvailableControllerPort({ports[0].device,ports[1].device},SDL_GetGamepadPlayerIndexForID(id));
            if(slot<0)break;
            if(auto* selected=SDL_OpenGamepad(id))Select(unsigned(slot),selected,id);
        }
        SDL_free(ids);
    }
    // The input-only integration consumes events through its watch.
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
}
GamepadSnapshot SdlGamepad::Poll(unsigned player) {
    impl_->Update();
    return impl_->Read(player);
}
std::array<GamepadSnapshot,2> SdlGamepad::PollAll() {
    impl_->Update();
    return {impl_->Read(0),impl_->Read(1)};
}
GamepadSnapshot SdlGamepad::Impl::Read(unsigned index) {
    GamepadSnapshot out;
    const auto& port=ports.at(cave::ControllerPort(index,swap_players));
    if (!port.gamepad) return out;
    JoystickLock joystick_lock;
    std::lock_guard lock(mutex);
    out.device=port.device;
    if (!focused) return out;
    out.pending=port.pending; out.edges=port.edges;
    for (unsigned button=0;button<SDL_GAMEPAD_BUTTON_COUNT;++button) {
        if (SDL_GetGamepadButton(port.gamepad,static_cast<SDL_GamepadButton>(button))) out.buttons|=Mask(button);
    }
    const auto stick = [&](SDL_GamepadAxis x, SDL_GamepadAxis y) {
        const auto raw = std::pair{SDL_GetGamepadAxis(port.gamepad,x), SDL_GetGamepadAxis(port.gamepad,y)};
        return apply_deadzone ? cave::ApplyStickDeadzone(raw.first, raw.second, deadzone) : raw;
    };
    const auto left = stick(SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY);
    const auto right = stick(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY);
    out.left_x=Axis(left.first); out.left_y=Axis(left.second);
    out.right_x=Axis(right.first); out.right_y=Axis(right.second);
    out.l2=Trigger(SDL_GetGamepadAxis(port.gamepad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    out.r2=Trigger(SDL_GetGamepadAxis(port.gamepad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    if (out.l2>30) out.buttons|=0x100;
    if (out.r2>30) out.buttons|=0x200;
    return out;
}
void SdlGamepad::Acknowledge(const GamepadSnapshot& delivered) {
    std::lock_guard lock(impl_->mutex);
    if (!delivered.device) return;
    for (auto& port:impl_->ports) {
        if (delivered.device!=port.device) continue;
        for (unsigned bit=0;bit<port.edges.size();++bit)
            if ((delivered.pending&(1u<<bit)) && delivered.edges[bit]==port.edges[bit]) port.pending&=~(1u<<bit);
    }
}
int SdlGamepadMonitor(unsigned seconds) {
    SdlGamepad backend; backend.Initialize(); backend.Focus(true);
    auto previous=GamepadSnapshot{};
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
    unsigned changes=0;
    unsigned previous_hat=0;
    do {
        const auto state=backend.Poll();
        unsigned raw_hat=0;
        if (backend.impl_->ports[0].gamepad) {
            auto* joystick=SDL_GetGamepadJoystick(backend.impl_->ports[0].gamepad);
            if (SDL_GetNumJoystickHats(joystick)>0) raw_hat=SDL_GetJoystickHat(joystick,0);
        }
        if (state.device!=previous.device || state.buttons!=previous.buttons || state.left_x!=previous.left_x ||
            state.left_y!=previous.left_y || state.right_x!=previous.right_x || state.right_y!=previous.right_y || state.l2!=previous.l2 || state.r2!=previous.r2 || raw_hat!=previous_hat) {
            std::printf("{\"event\":\"native_sdl_gamepad_monitor\",\"elapsed_ms\":%llu,\"device\":%u,\"buttons\":%u,\"raw_hat\":%u,\"left\":[%u,%u],\"right\":[%u,%u],\"triggers\":[%u,%u]}\n",
                SDL_GetTicks(),state.device,state.buttons,raw_hat,state.left_x,state.left_y,state.right_x,state.right_y,state.l2,state.r2);
            ++changes; previous=state; previous_hat=raw_hat;
        }
        backend.Acknowledge(state);
        if (seconds) std::this_thread::sleep_for(std::chrono::milliseconds(4));
    } while (std::chrono::steady_clock::now()<end);
    std::printf("{\"event\":\"native_sdl_gamepad_monitor_complete\",\"changes\":%u,\"device\":%u}\n",changes,previous.device);
    return 0;
}
bool SdlGamepadSelfTest() {
    {
        SdlGamepad disabled; disabled.Configure(false,0.2); disabled.Initialize();
        if (disabled.impl_->initialized || disabled.Poll().device || disabled.Poll().buttons) return false;
        for (double invalid : {-0.1,1.0,std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected{};
            try { disabled.Configure(true,invalid); } catch (const std::runtime_error&) { rejected=true; }
            if (!rejected) return false;
        }
    }
    SdlGamepad backend; backend.Initialize();
    SDL_VirtualJoystickDesc descriptor; SDL_INIT_INTERFACE(&descriptor);
    descriptor.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    descriptor.naxes=SDL_GAMEPAD_AXIS_COUNT; descriptor.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    descriptor.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    descriptor.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    descriptor.name="Ketsui authored virtual gamepad";
    const auto id=SDL_AttachVirtualJoystick(&descriptor);
    if (!id) return false;
    auto* joystick=SDL_OpenJoystick(id);
    if (!joystick) { SDL_DetachVirtualJoystick(id); return false; }
    backend.impl_->test_devices[0]=id;
    backend.Focus(true);
    int passed=0; bool okay=true;
    const auto check=[&](bool result,const char* name) {
        std::printf("{\"event\":\"sdl_gamepad_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n",name,result?"true":"false");
        okay&=result; if(result)++passed;
    };
    auto state=backend.Poll();
    check(state.device==id && !state.buttons && state.left_x==128 && state.left_y==128,"virtual_device_selected_without_physical_hardware");
    for (unsigned button=0;button<SDL_GAMEPAD_BUTTON_COUNT;++button) {
        SDL_SetJoystickVirtualButton(joystick,static_cast<int>(button),true);
        state=backend.Poll();
        check(state.buttons==Mask(button),"standard_button_to_ps4_mapping");
        backend.Acknowledge(state);
        SDL_SetJoystickVirtualButton(joystick,static_cast<int>(button),false);
        state=backend.Poll(); backend.Acknowledge(state);
    }
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,-32768);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,32767);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTX,32767);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,-32768);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);
    state=backend.Poll();
    check(state.left_x==0 && state.left_y==255 && state.right_x==255 && state.right_y==0 && state.l2==255 && (state.buttons&0x100),"axes_and_trigger_orientation");
    backend.Acknowledge(state);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768); backend.Poll();
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true); backend.Poll();
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false);
    const auto short_press=backend.Poll();
    check(!(short_press.buttons&0x4000) && (short_press.pending&0x4000),"released_short_press_is_dirty_but_not_held");
    backend.Acknowledge(short_press);
    check(!(backend.Poll().buttons&0x4000),"acknowledged_short_press_not_replayed");
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true); backend.Poll();
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false); backend.Poll();
    backend.Acknowledge(short_press);
    check((backend.Poll().pending&0x4000)!=0,"old_acknowledgement_cannot_erase_new_edge");
    backend.Focus(false);
    check(!backend.Poll().buttons && !backend.HasPending(),"focus_loss_clears_buttons_and_pending");
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_NORTH,true); backend.Poll();
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_NORTH,false); backend.Poll();
    backend.Focus(true);
    check(!backend.Poll().buttons,"background_short_press_not_replayed_on_refocus");
    const auto twin_id=SDL_AttachVirtualJoystick(&descriptor);
    auto* twin=SDL_OpenJoystick(twin_id);
    backend.impl_->test_devices[1]=twin_id;
    backend.impl_->next_scan=0;
    check(twin && twin_id!=id &&
          std::ranges::equal(SDL_GetGamepadGUIDForID(id).data,SDL_GetGamepadGUIDForID(twin_id).data),
          "cabinet_halves_have_same_guid_and_distinct_instances");
    if (twin) {
        SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_LEFT,true);
        SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true);
        SDL_SetJoystickVirtualButton(twin,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,true);
        SDL_SetJoystickVirtualButton(twin,SDL_GAMEPAD_BUTTON_SOUTH,true);
        SDL_SetJoystickVirtualButton(twin,SDL_GAMEPAD_BUTTON_START,true);
        auto inputs=backend.PollAll();
        check(inputs[0].device==id && inputs[1].device==twin_id &&
              (inputs[0].buttons&0x40a8)==0x4080 && (inputs[1].buttons&0x40a8)==0x4028,
              "same_model_opposite_directions_both_shoot_only_second_starts");
        {
            SdlGamepad reversed;
            reversed.Configure(true,0.2,true);
            reversed.Initialize();
            reversed.Focus(true);
            reversed.impl_->test_devices={id,twin_id};
            const auto swapped=reversed.PollAll();
            check(swapped[0].device==twin_id && swapped[1].device==id &&
                  (swapped[0].buttons&0x40a8)==0x4028 && (swapped[1].buttons&0x40a8)==0x4080,
                  "saved_swap_routes_both_controllers_to_opposite_players");
        }
        SDL_SetJoystickVirtualButton(twin,SDL_GAMEPAD_BUTTON_SOUTH,false);
        backend.Poll(1);
        SDL_SetJoystickVirtualButton(twin,SDL_GAMEPAD_BUTTON_SOUTH,true);
        backend.Poll(1);
        backend.ClearPending(0);
        check(backend.HasPending(1),"first_player_acknowledgement_keeps_second_edges");
    }
    SDL_CloseJoystick(joystick);
    SDL_DetachVirtualJoystick(id);
    const auto unplugged=backend.PollAll();
    check(!unplugged[0].device && !backend.HasPending() && unplugged[1].device==twin_id,
          "removing_first_device_keeps_second_player_in_place");
    const auto reconnected=SDL_AttachVirtualJoystick(&descriptor);
    backend.impl_->test_devices[0]=reconnected;
    backend.impl_->next_scan=0;
    const auto restored=backend.PollAll();
    check(reconnected && restored[0].device==reconnected && restored[1].device==twin_id,
          "reconnected_first_device_fills_only_vacant_seat");
    if(twin)SDL_CloseJoystick(twin);
    if(twin_id)SDL_DetachVirtualJoystick(twin_id);
    if(reconnected)SDL_DetachVirtualJoystick(reconnected);
    backend.PollAll();
    // Most real fightsticks use a POV hat, not four separate joystick buttons.
    descriptor.nhats=1;
    const auto hat_id=SDL_AttachVirtualJoystick(&descriptor);
    auto* hat_joystick=SDL_OpenJoystick(hat_id);
    char hat_guid[33]{}; SDL_GUIDToString(SDL_GetGamepadGUIDForID(hat_id),hat_guid,sizeof(hat_guid));
    const std::string hat_mapping=std::string(hat_guid)+",Ketsui POV fixture,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,";
    check(hat_id && hat_joystick && SDL_SetGamepadMapping(hat_id,hat_mapping.c_str()),"pov_hat_fixture_mapping");
    backend.impl_->test_devices[0]=hat_id; backend.impl_->next_scan=0;
    backend.Poll();
    if (hat_joystick) {
        for (const auto [hat,mask]:{std::pair<unsigned,std::uint32_t>{1,0x10},{4,0x40},{8,0x80},{2,0x20},{3,0x30},{12,0xc0}}) {
            SDL_SetJoystickVirtualHat(hat_joystick,0,static_cast<Uint8>(hat));
            state=backend.Poll();
            check(state.buttons==mask,"pov_hat_cardinal_and_diagonal");
            backend.Acknowledge(state);
            SDL_SetJoystickVirtualHat(hat_joystick,0,0);
            state=backend.Poll(); backend.Acknowledge(state);
            check(!state.buttons,"pov_hat_returns_to_neutral");
        }
        SDL_CloseJoystick(hat_joystick);
    }
    if (hat_id) SDL_DetachVirtualJoystick(hat_id);
    backend.Poll();
    bool late_rejected{};
    try { backend.Configure(false,0.2); } catch (const std::runtime_error&) { late_rejected=true; }
    check(late_rejected,"late_gamepad_configuration_rejected");
    {
        SdlGamepad configured; configured.Configure(true,0.25); configured.Initialize(); configured.Focus(true);
        descriptor.nhats=0;
        descriptor.name="CAVE configured axis fixture"; // Not the earlier POV-only mapping's GUID.
        const auto configured_id=SDL_AttachVirtualJoystick(&descriptor);
        auto* configured_joystick=SDL_OpenJoystick(configured_id);
        configured.impl_->test_devices[0]=configured_id;
        check(configured_id && configured_joystick,"configured_virtual_device_created");
        if (configured_joystick) {
            SDL_SetJoystickVirtualAxis(configured_joystick,SDL_GAMEPAD_AXIS_LEFTX,4000);
            SDL_SetJoystickVirtualAxis(configured_joystick,SDL_GAMEPAD_AXIS_LEFTY,-4000);
            auto sample=configured.Poll();
            check(sample.left_x==128 && sample.left_y==128,"configured_radial_deadzone_centers_small_axes");
            SDL_SetJoystickVirtualAxis(configured_joystick,SDL_GAMEPAD_AXIS_LEFTX,32767);
            SDL_SetJoystickVirtualAxis(configured_joystick,SDL_GAMEPAD_AXIS_LEFTY,0);
            SDL_SetJoystickVirtualButton(configured_joystick,SDL_GAMEPAD_BUTTON_DPAD_LEFT,true);
            sample=configured.Poll();
            check(sample.left_x==255 && sample.left_y==128 && (sample.buttons&0x80),"deadzone_keeps_full_range_and_digital_hat");
            SDL_SetJoystickVirtualAxis(configured_joystick,SDL_GAMEPAD_AXIS_LEFTX,16384);
            sample=configured.Poll();
            const auto expected=cave::ApplyStickDeadzone(16384,0,0.25);
            check(sample.left_x==Axis(expected.first) && sample.left_y==Axis(expected.second),"ps4_and_360_use_same_deadzone_curve");
            SDL_CloseJoystick(configured_joystick);
        }
        if (configured_id) SDL_DetachVirtualJoystick(configured_id);
        configured.Poll();
    }
    std::printf("{\"event\":\"sdl_gamepad_self_test\",\"passed\":%d,\"success\":%s}\n",passed,okay?"true":"false");
    return okay;
}
}
