#include "input/input_state.h"
#include "input/configured_input.h"
#include <cmath>
#include <array>
#include <iostream>
#include <stdexcept>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/input/input_driver.h>
#include <rex/ui/keybinds.h>
using namespace espgaluda2;
using rex::X_RESULT;
void Require(bool condition,const char* reason) {if (!condition) throw std::runtime_error(reason);}
class KeystrokeDriver final : public rex::input::InputDriver {
public:
    explicit KeystrokeDriver(bool empty):InputDriver(nullptr,0),empty_(empty){}
    rex::X_STATUS Setup() override {return 0;}
    rex::X_RESULT GetCapabilities(uint32_t,uint32_t,rex::input::X_INPUT_CAPABILITIES*) override {return 0;}
    rex::X_RESULT GetState(uint32_t,rex::input::X_INPUT_STATE*) override {return 0;}
    rex::X_RESULT SetState(uint32_t,rex::input::X_INPUT_VIBRATION*) override {return 0;}
    rex::X_RESULT GetKeystroke(uint32_t,uint32_t,rex::input::X_INPUT_KEYSTROKE* output) override {
        if(empty_)return X_ERROR_EMPTY;
        *output={}; output->virtual_key=0x5800; return 0;
    }
private:bool empty_;
};
class StateDriver final : public rex::input::InputDriver {
public:
    explicit StateDriver(bool keyboard):InputDriver(nullptr,0),keyboard_(keyboard) {connected[0]=true;}
    rex::X_STATUS Setup() override {return 0;}
    bool IsKeyboardEmulation() const override {return keyboard_;}
    rex::X_RESULT GetCapabilities(uint32_t,uint32_t,rex::input::X_INPUT_CAPABILITIES*) override {return X_ERROR_DEVICE_NOT_CONNECTED;}
    rex::X_RESULT GetState(uint32_t user,rex::input::X_INPUT_STATE* output) override {
        if(user>=connected.size() || !connected[user]) return X_ERROR_DEVICE_NOT_CONNECTED;
        *output=states[user];return 0;
    }
    rex::X_RESULT SetState(uint32_t,rex::input::X_INPUT_VIBRATION*) override {return 0;}
    rex::X_RESULT GetKeystroke(uint32_t,uint32_t,rex::input::X_INPUT_KEYSTROKE*) override {return X_ERROR_EMPTY;}
    std::array<rex::input::X_INPUT_STATE,4> states{};
    std::array<bool,4> connected{};
private:bool keyboard_;
};
void TestMergedPackets() {
    rex::input::InputSystem input(nullptr);
    auto pad=std::make_unique<StateDriver>(false);
    auto keyboard=std::make_unique<StateDriver>(true);
    auto* physical=pad.get();auto* keys=keyboard.get();
    physical->states[0].packet_number=1000;
    keys->states[0].packet_number=1;
    input.AddDriver(std::move(pad));input.AddDriver(std::move(keyboard));
    rex::input::X_INPUT_STATE output{};
    Require(input.GetState(0,&output)==0,"Merged initial state");
    uint32_t previous=output.packet_number;
    keys->states[0].packet_number=2;keys->states[0].gamepad.buttons=0x1000;
    Require(input.GetState(0,&output)==0 && output.gamepad.buttons==0x1000 && output.packet_number!=previous,
            "High idle pad packet hid a keyboard press");
    previous=output.packet_number;
    keys->states[0].packet_number=3;keys->states[0].gamepad.buttons=0;
    Require(input.GetState(0,&output)==0 && output.gamepad.buttons==0 && output.packet_number!=previous,
            "High idle pad packet hid a keyboard release");
    previous=output.packet_number;
    keys->states[0].packet_number=10000;
    Require(input.GetState(0,&output)==0 && output.packet_number==previous,
            "Backend packet changed an identical merged state");
    physical->states[0].packet_number=1;physical->states[0].gamepad.buttons=0x2000;
    Require(input.GetState(0,&output)==0 && output.gamepad.buttons==0x2000 && output.packet_number!=previous,
            "Backend packet reset hid a physical pad transition");
    previous=output.packet_number;
    keys->states[0].gamepad.buttons=0x1000;
    keys->states[0].gamepad.left_trigger=90;
    physical->states[0].gamepad.left_trigger=200;
    physical->states[0].gamepad.thumb_lx=-32768;
    keys->states[0].gamepad.thumb_lx=100;
    Require(input.GetState(0,&output)==0 && output.gamepad.buttons==0x3000 &&
            output.gamepad.left_trigger==200 && output.gamepad.thumb_lx==-32768 && output.packet_number!=previous,
            "Merged gamepad fields or packet changed incorrectly");
    previous=output.packet_number;
    physical->states[0].gamepad.left_trigger=201;
    Require(input.GetState(0,&output)==0 && output.gamepad.left_trigger==201 && output.packet_number!=previous,
            "Trigger-only transition did not advance packet");
    previous=output.packet_number;
    physical->states[0].gamepad.thumb_ly=-17000;
    Require(input.GetState(0,&output)==0 && output.gamepad.thumb_ly==-17000 && output.packet_number!=previous,
            "Axis-only transition did not advance packet");
    previous=output.packet_number;
    physical->connected[1]=true;
    Require(input.GetState(1,&output)==0,"Independent second player");
    Require(input.GetState(0,&output)==0 && output.packet_number==previous,"Another player changed packet identity");
    physical->connected[0]=false;keys->connected[0]=false;
    Require(input.GetState(0,&output)==X_ERROR_DEVICE_NOT_CONNECTED,"Disconnected merged controller");
    physical->connected[0]=true;keys->connected[0]=true;
    Require(input.GetState(0,&output)==0 && output.packet_number!=previous,"Reconnection retained stale identity");
}
int main() {
    try {
        auto settings=ParseSettings("[keyboard]\na=Z,Numpad0\n");
        KeyboardState keys(settings);
        keys.SetKey(90,true);
        Require(!keys.Snapshot().buttons,"Input accepted without focus");
        keys.SetFocus(true); keys.SetKey(90,true);
        Require(keys.Snapshot().buttons==0x1000,"A mapping");
        keys.SetKey(96,true); keys.SetKey(90,false);
        Require(keys.Snapshot().buttons==0x1000,"Alternate key release dropped held button");
        keys.SetKey(38,true); keys.SetKey(39,true); keys.SetKey(81,true); keys.SetKey(87,true);
        Require(keys.Snapshot().buttons==0x1009 && keys.Snapshot().left_trigger==255 &&
                keys.Snapshot().right_trigger==255,"Diagonal and triggers");
        Require(keys.Snapshot(true,96).buttons==0x1000 && !keys.Snapshot(true,96).left_trigger,
                "Repeat of one key leaked unrelated held buttons");
        Require(keys.Snapshot(false)==KeyboardSnapshot{},"Interactive overlay failed to mute keyboard");
        keys.SetFocus(false); keys.SetFocus(true);
        Require(keys.Snapshot()==KeyboardSnapshot{},"Stuck keys after refocus");
        keys.SetKey(999,true);
        Require(keys.Snapshot()==KeyboardSnapshot{},"Out-of-range key accepted");
        KeyboardState modifiers(ParseSettings("[keyboard]\na=Shift\n"));
        modifiers.SetFocus(true); modifiers.SetKey(160,true); modifiers.SetKey(161,true); modifiers.SetKey(160,false);
        Require(modifiers.Snapshot().buttons==0x1000,"Left/right modifier alias lost held side");
        Require(modifiers.Snapshot(true,161).buttons==0x1000,"Modifier repeat alias mapping");
        modifiers.SetKey(161,false);
        Require(!modifiers.Snapshot().buttons,"Modifier release remained stuck");
        KeyboardState taps(ParseSettings("[keyboard]\na=Z,Numpad0\n"));
        taps.SetFocus(true);
        taps.SetKey(13,true); taps.SetKey(13,false);
        Require(!taps.Snapshot().buttons,"Tap latch altered live keystroke state");
        Require(taps.Poll().buttons==0x10 && !taps.Poll().buttons,"Quick Start tap lost or persisted beyond one poll");
        taps.SetKey(90,true); Require(taps.Poll().buttons==0x1000,"Held key not sampled");
        taps.SetKey(90,false); Require(!taps.Poll().buttons,"Sampled hold release was delayed");
        taps.SetKey(90,true); taps.SetKey(90,false); taps.SetKey(96,true);
        Require(taps.Poll().buttons==0x1000,"Alternate key and quick tap mapping");
        taps.SetKey(96,false); Require(!taps.Poll().buttons,"Alternate key release remained latched");
        taps.SetKey(81,true); taps.SetKey(81,false);
        Require(taps.Poll().left_trigger==255 && !taps.Poll().left_trigger,"Quick trigger tap");
        taps.SetKey(90,true); taps.SetKey(90,false); taps.SetFocus(false); taps.SetFocus(true);
        Require(!taps.Poll().buttons,"Quick tap survived focus loss");
        taps.SetKey(90,true); taps.SetKey(90,false);
        Require(!taps.Poll(false).buttons && !taps.Poll().buttons,"Muted overlay replayed a stale tap");
        modifiers.SetKey(160,true); modifiers.SetKey(160,false);
        Require(modifiers.Poll().buttons==0x1000 && !modifiers.Poll().buttons,"Quick modifier alias tap");
        Require(ApplyDeadzone(1000,-1000,.2)==std::pair<int16_t,int16_t>{},"Deadzone center");
        Require(ApplyDeadzone(32767,0,.2)==std::pair<int16_t,int16_t>{32767,0},"Positive full scale");
        Require(ApplyDeadzone(-32768,0,.2)==std::pair<int16_t,int16_t>{-32768,0},"Negative full scale");
        auto diagonal=ApplyDeadzone(32767,32767,.2);
        Require(std::abs(diagonal.first-diagonal.second)<=1 && diagonal.first>23000 && diagonal.first<23200,"Radial diagonal");
        auto half=ApplyDeadzone(16384,0,.2);
        Require(half.first>12200 && half.first<12400 && !half.second,"Continuous deadzone rescaling");
        for (auto [name,code] : std::initializer_list<std::pair<const char*,uint16_t>>{
                {"Return",13},{"Z",90},{"Up",38},{"Backspace",8},{"Numpad0",96},{"F12",123}})
            Require(static_cast<uint16_t>(rex::ui::ParseVirtualKey(name))==KeyCode(name) && KeyCode(name)==code,"SDK key mismatch");
        settings.gamepad_enabled=false;
        auto input=CreateConfiguredInput(settings,false);
        auto* concrete=static_cast<rex::input::InputSystem*>(input.get());
        rex::input::X_INPUT_STATE state{};
        rex::input::X_INPUT_CAPABILITIES caps{};
        Require(concrete->GetCapabilities(0,0,&caps)==0 && caps.type==1,"Keyboard controller capabilities");
        Require(concrete->GetState(0,&state)==0 && !static_cast<uint16_t>(state.gamepad.buttons),"Unfocused controller state");
        Require(concrete->GetState(1,&state)!=0,"Phantom second controller");
        bool rejected{};
        try {CreateConfiguredInput(settings,true);} catch (const std::runtime_error&) {rejected=true;}
        Require(rejected,"Tool mode allowed player input initialization");
        rex::input::InputSystem merged(nullptr);
        merged.AddDriver(std::make_unique<KeystrokeDriver>(true));
        merged.AddDriver(std::make_unique<KeystrokeDriver>(false));
        rex::input::X_INPUT_KEYSTROKE stroke{};
        Require(merged.GetKeystroke(0,0,&stroke)==0 && static_cast<uint16_t>(stroke.virtual_key)==0x5800,
                "Idle driver suppressed another controller's keystroke");
        TestMergedPackets();
        std::cout<<"Input regressions passed; no SDL gamepad, audio or window initialized\n";
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n'; return 2;}
}
