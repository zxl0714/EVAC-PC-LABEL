#include "runtime/ps2_pad.h"
#include "ps2_host_backend.h"
#include <cstring>
#include "../../../backends/ps2/diagnostic_input.h"
#include "../../../backends/ps2/host_input.h"
#include <array>
#include <atomic>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
    constexpr uint8_t kPadAnalogMarker = 0x73;
    constexpr uint8_t kPadStickCenter = 0x80;

    constexpr uint16_t PAD_LEFT = 0x0080u;
    constexpr uint16_t PAD_DOWN = 0x0040u;
    constexpr uint16_t PAD_RIGHT = 0x0020u;
    constexpr uint16_t PAD_UP = 0x0010u;
    constexpr uint16_t PAD_START = 0x0008u;
    constexpr uint16_t PAD_R3 = 0x0004u;
    constexpr uint16_t PAD_L3 = 0x0002u;
    constexpr uint16_t PAD_SELECT = 0x0001u;
    constexpr uint16_t PAD_SQUARE = 0x8000u;
    constexpr uint16_t PAD_CROSS = 0x4000u;
    constexpr uint16_t PAD_CIRCLE = 0x2000u;
    constexpr uint16_t PAD_TRIANGLE = 0x1000u;
    constexpr uint16_t PAD_R1 = 0x0800u;
    constexpr uint16_t PAD_L1 = 0x0400u;
    constexpr uint16_t PAD_R2 = 0x0200u;
    constexpr uint16_t PAD_L2 = 0x0100u;
    std::atomic<uint32_t> diagnosticTapMs{};
    std::atomic<uint16_t> diagnosticButtons{};
    std::array<uint64_t,16> diagnosticDeadlines{};
    std::array<std::atomic<uint16_t>,2> diagnosticLastRead{0xffffu,0xffffu};
    std::atomic<std::shared_ptr<cave::ps2::HostPadSource>> hostSource;

    uint16_t diagnosticKeyMask(int key)
    {
        switch(key) {
        case KEY_UP:case KEY_W:return PAD_UP;
        case KEY_DOWN:case KEY_S:return PAD_DOWN;
        case KEY_LEFT:case KEY_A:return PAD_LEFT;
        case KEY_RIGHT:case KEY_D:return PAD_RIGHT;
        case KEY_X:case KEY_SPACE:return PAD_CROSS;
        case KEY_C:case KEY_ESCAPE:return PAD_CIRCLE;
        case KEY_Z:case KEY_KP_0:return PAD_SQUARE;
        case KEY_V:case KEY_KP_1:return PAD_TRIANGLE;
        case KEY_Q:return PAD_L1;
        case KEY_E:return PAD_R1;
        case KEY_LEFT_SHIFT:return PAD_L2;
        case KEY_RIGHT_SHIFT:return PAD_R2;
        case KEY_ENTER:return PAD_START;
        case KEY_TAB:return PAD_SELECT;
        default:return 0;
        }
    }
}

namespace cave::ps2 {
void SetHostPadSource(std::shared_ptr<HostPadSource> source){hostSource.store(std::move(source),std::memory_order_release);}
uint32_t DiagnosticTapMilliseconds(){return diagnosticTapMs.load(std::memory_order_relaxed);}
void ConfigureDiagnosticInput(uint32_t milliseconds)
{
    if(milliseconds>500)throw std::invalid_argument("Diagnostic tap duration must be 0..500 ms");
    diagnosticDeadlines.fill(0);
    diagnosticButtons.store(0);
    for(auto& value:diagnosticLastRead)value.store(0xffffu);
    diagnosticTapMs.store(milliseconds);
}

void PollDiagnosticInput(uint64_t now)
{
    const auto duration=diagnosticTapMs.load(std::memory_order_relaxed);
    if(!duration)return;
    for(int key=GetKeyPressed();key;key=GetKeyPressed()) {
        const auto mask=diagnosticKeyMask(key);
        if(!mask)continue;
        for(unsigned bit=0;bit<16;++bit)
            if(mask&(1u<<bit))diagnosticDeadlines[bit]=now+duration;
        std::ostringstream line;
        line<<"[ps2-pad-event] key="<<key<<" active=0x"<<std::hex<<mask
            <<std::dec<<" pulse_ms="<<duration<<'\n';
        std::cerr<<line.str();
    }
    uint16_t active{};
    for(unsigned bit=0;bit<16;++bit)
        if(now<diagnosticDeadlines[bit])active|=uint16_t(1u<<bit);
    diagnosticButtons.store(active,std::memory_order_release);
}
}

bool PSPadBackend::readState(int port, int slot, uint8_t *data, size_t size)
{
    if (!data || size < 32)
        return false;

    std::memset(data, 0, 32);
    data[0] = 0x01;
    data[1] = kPadAnalogMarker;
    data[2] = 0xFF;
    data[3] = 0xFF;
    data[4] = data[5] = data[6] = data[7] = kPadStickCenter;

    if(const auto source=hostSource.load(std::memory_order_acquire)) {
        const auto snapshot=source->Read(unsigned(port));
        const auto buttons=uint16_t(~snapshot.buttons);
        data[2]=uint8_t(buttons);data[3]=uint8_t(buttons>>8);
        std::copy(snapshot.axes.begin(),snapshot.axes.end(),data+4);
        if(diagnosticTapMs.load(std::memory_order_relaxed) && port>=0 && port<2 &&
           diagnosticLastRead[port].exchange(buttons)!=buttons)
            std::cerr<<"[ps2-pad-read] port="<<port<<" slot="<<slot<<" active=0x"
                <<std::hex<<snapshot.buttons<<std::dec<<'\n';
        return true;
    }

    uint16_t btns = 0xFFFFu;
    constexpr int kGamepad = 0;
    const bool useGamepad = IsGamepadAvailable(kGamepad);
    auto clearBit = [&btns](uint16_t mask)
    { btns &= ~mask; };

    if (useGamepad)
    {
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_UP))
            clearBit(PAD_UP);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN))
            clearBit(PAD_DOWN);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT))
            clearBit(PAD_LEFT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
            clearBit(PAD_RIGHT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
            clearBit(PAD_CROSS);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))
            clearBit(PAD_CIRCLE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))
            clearBit(PAD_SQUARE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_UP))
            clearBit(PAD_TRIANGLE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_1))
            clearBit(PAD_L1);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))
            clearBit(PAD_R1);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_2))
            clearBit(PAD_L2);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2))
            clearBit(PAD_R2);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_MIDDLE_RIGHT))
            clearBit(PAD_START);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_MIDDLE_LEFT))
            clearBit(PAD_SELECT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_THUMB))
            clearBit(PAD_L3);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_THUMB))
            clearBit(PAD_R3);

        float lx = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_X);
        float ly = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_Y);
        float rx = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_RIGHT_X);
        float ry = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_RIGHT_Y);
        data[6] = static_cast<uint8_t>(128 + lx * 127);
        data[7] = static_cast<uint8_t>(128 + ly * 127);
        data[4] = static_cast<uint8_t>(128 + rx * 127);
        data[5] = static_cast<uint8_t>(128 + ry * 127);
    }
    // Keyboard input remains available while a controller is connected.
    {
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
            clearBit(PAD_UP);
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
            clearBit(PAD_DOWN);
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
            clearBit(PAD_LEFT);
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
            clearBit(PAD_RIGHT);
        if (IsKeyDown(KEY_X) || IsKeyDown(KEY_SPACE))
            clearBit(PAD_CROSS);
        if (IsKeyDown(KEY_C) || IsKeyDown(KEY_ESCAPE))
            clearBit(PAD_CIRCLE);
        if (IsKeyDown(KEY_Z) || IsKeyDown(KEY_KP_0))
            clearBit(PAD_SQUARE);
        if (IsKeyDown(KEY_V) || IsKeyDown(KEY_KP_1))
            clearBit(PAD_TRIANGLE);
        if (IsKeyDown(KEY_Q))
            clearBit(PAD_L1);
        if (IsKeyDown(KEY_E))
            clearBit(PAD_R1);
        if (IsKeyDown(KEY_LEFT_SHIFT))
            clearBit(PAD_L2);
        if (IsKeyDown(KEY_RIGHT_SHIFT))
            clearBit(PAD_R2);
        if (IsKeyDown(KEY_ENTER))
            clearBit(PAD_START);
        if (IsKeyDown(KEY_TAB))
            clearBit(PAD_SELECT);
    }

    if(diagnosticTapMs.load(std::memory_order_relaxed)) {
        if(port==0)btns&=uint16_t(~diagnosticButtons.load(std::memory_order_acquire));
        if(port>=0 && port<2 && diagnosticLastRead[port].exchange(btns)!=btns) {
            std::ostringstream line;
            line<<"[ps2-pad-read] port="<<port<<" slot="<<slot<<" active=0x"
                <<std::hex<<uint16_t(~btns)<<std::dec<<'\n';
            std::cerr<<line.str();
        }
    }
    data[2] = static_cast<uint8_t>(btns & 0xFF);
    data[3] = static_cast<uint8_t>(btns >> 8);
    return true;
}
