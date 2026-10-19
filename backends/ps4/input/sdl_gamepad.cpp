#include "sdl_gamepad.h"
#include "common/stick_deadzone.h"
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
    bool apply_deadzone{};
    double deadzone{}; // Legacy diagnostic mode preserves unfiltered axes.
    SDL_Gamepad* gamepad{};
    SDL_JoystickID device{}, preferred{};
    Uint64 next_scan{};
    std::mutex mutex;
    std::uint32_t pending{};
    std::array<std::uint64_t,32> edges{};
    std::uint32_t trigger_held{};
    void Press(std::uint32_t mask) { // event mutex held, no calls into SDL
        if (!focused) return;
        pending|=mask;
        for (unsigned bit=0;bit<32;++bit) if (mask&(1u<<bit)) ++edges[bit];
    }
    static bool SDLCALL Watch(void* context, SDL_Event* event) {
        auto& self=*static_cast<Impl*>(context);
        if (event->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            std::lock_guard lock(self.mutex);
            if (event->gbutton.which==self.device) self.Press(Mask(event->gbutton.button));
        } else if (event->type==SDL_EVENT_GAMEPAD_AXIS_MOTION &&
                   (event->gaxis.axis==SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event->gaxis.axis==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
            std::lock_guard lock(self.mutex);
            if (event->gaxis.which==self.device) {
                const std::uint32_t bit=event->gaxis.axis==SDL_GAMEPAD_AXIS_LEFT_TRIGGER?0x100:0x200;
                if (Trigger(event->gaxis.value)>30) {
                    if (!(self.trigger_held&bit)) self.Press(bit);
                    self.trigger_held|=bit;
                } else self.trigger_held&=~bit;
            }
        }
        return true;
    }
    void Select(SDL_Gamepad* selected, SDL_JoystickID id) {
        { std::lock_guard lock(mutex); device=id; pending=trigger_held=0; edges.fill(0); }
        if (gamepad) SDL_CloseGamepad(gamepad);
        gamepad=selected;
        if (gamepad) {
            char guid[33]{}; SDL_GUIDToString(SDL_GetGamepadGUIDForID(id),guid,sizeof(guid));
            std::printf("{\"event\":\"native_sdl_gamepad_connected\",\"device\":%u,\"name\":%s,\"guid\":\"%s\",\"vendor_id\":%u,\"product_id\":%u,\"type\":%u}\n",
                id,Quoted(SDL_GetGamepadName(gamepad)).c_str(),guid,SDL_GetGamepadVendor(gamepad),SDL_GetGamepadProduct(gamepad),unsigned(SDL_GetGamepadType(gamepad)));
            char* mapping=SDL_GetGamepadMapping(gamepad);
            std::printf("{\"event\":\"native_sdl_gamepad_mapping\",\"mapping\":%s}\n",Quoted(mapping).c_str());
            SDL_free(mapping);
        } else std::puts("{\"event\":\"native_sdl_gamepad_disconnected\"}");
    }
    ~Impl() {
        if (!initialized) return;
        SDL_RemoveEventWatch(Watch,this);
        if (gamepad) SDL_CloseGamepad(gamepad);
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
};
SdlGamepad::SdlGamepad() : impl_(std::make_unique<Impl>()) {}
SdlGamepad::~SdlGamepad() = default;
void SdlGamepad::Configure(bool enabled, double deadzone) {
    if (!std::isfinite(deadzone) || deadzone < 0 || deadzone >= 1) throw std::runtime_error("Invalid gamepad deadzone");
    if (impl_->initialized) throw std::runtime_error("Gamepad configuration requires a restart");
    impl_->enabled = enabled;
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
    if (focused!=impl_->focused) impl_->pending=0;
    impl_->focused=focused;
}
void SdlGamepad::ClearPending() { std::lock_guard lock(impl_->mutex); impl_->pending=0; }
bool SdlGamepad::HasPending() { std::lock_guard lock(impl_->mutex); return impl_->pending!=0; }
GamepadSnapshot SdlGamepad::Poll() {
    GamepadSnapshot out;
    auto& self=*impl_;
    if (!self.initialized) return out;
    SDL_UpdateGamepads(); // explicitly documented as safe on any thread
    if (self.gamepad && !SDL_GamepadConnected(self.gamepad)) { self.Select(nullptr,0); self.next_scan=0; }
    if (!self.gamepad && SDL_GetTicks()>=self.next_scan) {
        self.next_scan=SDL_GetTicks()+250;
        int count=0;
        auto* ids=SDL_GetGamepads(&count);
        for (int i=0;ids && i<count;++i) {
            if (self.preferred && ids[i]!=self.preferred) continue;
            if (auto* selected=SDL_OpenGamepad(ids[i])) { self.Select(selected,ids[i]); break; }
        }
        SDL_free(ids);
    }
    // No SDL window/event-loop consumer exists in this input-only integration.
    // Watches have already latched real edges; do not retain an unbounded queue.
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    if (!self.gamepad) return out;
    JoystickLock joystick_lock;
    std::lock_guard lock(self.mutex);
    out.device=self.device;
    if (!self.focused) return out;
    out.pending=self.pending; out.edges=self.edges;
    out.buttons=self.pending;
    for (unsigned button=0;button<SDL_GAMEPAD_BUTTON_COUNT;++button)
        if (SDL_GetGamepadButton(self.gamepad,static_cast<SDL_GamepadButton>(button))) out.buttons|=Mask(button);
    const auto stick = [&](SDL_GamepadAxis x, SDL_GamepadAxis y) {
        const auto raw = std::pair{SDL_GetGamepadAxis(self.gamepad,x), SDL_GetGamepadAxis(self.gamepad,y)};
        return self.apply_deadzone ? cave::ApplyStickDeadzone(raw.first, raw.second, self.deadzone) : raw;
    };
    const auto left = stick(SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY);
    const auto right = stick(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY);
    out.left_x=Axis(left.first); out.left_y=Axis(left.second);
    out.right_x=Axis(right.first); out.right_y=Axis(right.second);
    out.l2=Trigger(SDL_GetGamepadAxis(self.gamepad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    out.r2=Trigger(SDL_GetGamepadAxis(self.gamepad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    if (out.l2>30) out.buttons|=0x100;
    if (out.r2>30) out.buttons|=0x200;
    if ((out.pending&0x100) && out.l2<=30) out.l2=255;
    if ((out.pending&0x200) && out.r2<=30) out.r2=255;
    return out;
}
void SdlGamepad::Acknowledge(const GamepadSnapshot& delivered) {
    std::lock_guard lock(impl_->mutex);
    if (delivered.device!=impl_->device) return;
    for (unsigned bit=0;bit<32;++bit)
        if ((delivered.pending&(1u<<bit)) && delivered.edges[bit]==impl_->edges[bit]) impl_->pending&=~(1u<<bit);
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
        if (backend.impl_->gamepad) {
            auto* joystick=SDL_GetGamepadJoystick(backend.impl_->gamepad);
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
    backend.impl_->preferred=id;
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
    check((short_press.buttons&0x4000) && (short_press.pending&0x4000),"short_down_up_retained_until_game_acknowledges");
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
    SDL_CloseJoystick(joystick);
    SDL_DetachVirtualJoystick(id);
    check(!backend.Poll().device && !backend.HasPending(),"disconnect_neutralizes_and_drops_pending");
    // Most real fightsticks use a POV hat, not four separate joystick buttons.
    descriptor.nhats=1;
    const auto hat_id=SDL_AttachVirtualJoystick(&descriptor);
    auto* hat_joystick=SDL_OpenJoystick(hat_id);
    char hat_guid[33]{}; SDL_GUIDToString(SDL_GetGamepadGUIDForID(hat_id),hat_guid,sizeof(hat_guid));
    const std::string hat_mapping=std::string(hat_guid)+",Ketsui POV fixture,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,";
    check(hat_id && hat_joystick && SDL_SetGamepadMapping(hat_id,hat_mapping.c_str()),"pov_hat_fixture_mapping");
    backend.impl_->preferred=hat_id; backend.impl_->next_scan=0;
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
        configured.impl_->preferred=configured_id;
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
