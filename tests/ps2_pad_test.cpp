#include "runtime/ps2_pad.h"
#include "raylib.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <deque>
#include "../backends/ps2/diagnostic_input.h"

namespace {
bool connected{};
std::array<bool,512> keys{};
std::array<bool,32> buttons{};
std::deque<int> pressedKeys;
void Require(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
}
extern "C" bool IsGamepadAvailable(int) {return connected;}
extern "C" bool IsGamepadButtonDown(int,int button) {return buttons.at(button);}
extern "C" float GetGamepadAxisMovement(int,int) {return 0;}
extern "C" bool IsKeyDown(int key) {return keys.at(key);}
extern "C" int GetKeyPressed() {
  if(pressedKeys.empty())return 0;
  const auto key=pressedKeys.front();pressedKeys.pop_front();return key;
}

int main() {
  try {
    PSPadBackend pad;
    std::array<uint8_t,32> data{};
    auto read=[&] {
      Require(pad.readState(0,0,data.data(),data.size()),"Pad read failed");
      return uint16_t(data[2]|(data[3]<<8));
    };
    Require(read()==0xffff,"Neutral buttons were active");
    keys[KEY_X]=true;
    Require((read()&0x4000)==0,"Keyboard Cross missing");
    connected=true;
    Require((read()&0x4000)==0,"Connected controller suppressed keyboard Cross");
    buttons[GAMEPAD_BUTTON_RIGHT_FACE_RIGHT]=true;
    keys[KEY_DOWN]=true;
    Require((read()&(0x4000|0x2000|0x0040))==0,"Combined keyboard/controller buttons lost");
    keys.fill(false);
    Require((read()&0x2000)==0 && (read()&0x4000)!=0,"Keyboard release cleared controller button");
    buttons.fill(false);
    Require(read()==0xffff,"Released buttons remained active");
    connected=false;keys[KEY_C]=true;keys[KEY_Z]=true;keys[KEY_ENTER]=true;
    Require((read()&(0x2000|0x8000|0x0008))==0,"Circle/Square/Start mapping changed");
    Require(!pad.readState(0,0,data.data(),31),"Short pad buffer was accepted");
    keys.fill(false);
    cave::ps2::ConfigureDiagnosticInput(200);
    pressedKeys.push_back(KEY_ENTER);
    cave::ps2::PollDiagnosticInput(1000);
    Require((read()&0x0008)==0,"Short queued Start was lost between host frames");
    Require(pad.readState(1,0,data.data(),32) && (data[2]&0x08)!=0,
            "Diagnostic primary-controller pulse leaked into player two");
    cave::ps2::PollDiagnosticInput(1199);
    Require((read()&0x0008)==0,"Diagnostic Start released before its configured duration");
    cave::ps2::PollDiagnosticInput(1200);
    Require(read()==0xffff,"Expired diagnostic pulse remained active");
    cave::ps2::ConfigureDiagnosticInput(0);
    pressedKeys.push_back(KEY_ENTER);cave::ps2::PollDiagnosticInput(1300);
    Require(read()==0xffff,"Ordinary input unexpectedly latched queued taps");
    std::cout<<"PS2 pad: connected-controller keyboard, merge and release passed\n";
    return 0;
  }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
