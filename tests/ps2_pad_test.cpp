#include "runtime/ps2_pad.h"
#include "raylib.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
bool connected{};
std::array<bool,512> keys{};
std::array<bool,32> buttons{};
void Require(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
}
extern "C" bool IsGamepadAvailable(int) {return connected;}
extern "C" bool IsGamepadButtonDown(int,int button) {return buttons.at(button);}
extern "C" float GetGamepadAxisMovement(int,int) {return 0;}
extern "C" bool IsKeyDown(int key) {return keys.at(key);}

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
    std::cout<<"PS2 pad: connected-controller keyboard, merge and release passed\n";
    return 0;
  }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
