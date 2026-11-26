#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

class PS2Runtime;
namespace cave::ps2 {
class NativeHost;
class GsDisplay;
class HostPadSource;
class StateMenu {
public:
    StateMenu(NativeHost& host,GsDisplay& display,PS2Runtime& runtime,std::filesystem::path directory,
              std::string identity,const std::filesystem::path& config,bool gamepad,bool keyboardEvents,
              HostPadSource& pad,bool circleConfirm);
    ~StateMenu();
    void Update();
    void RestoreOnEntry(unsigned slot,std::string previousError,std::function<void()> restore);
    void Key(unsigned code,bool down,uint16_t buttons);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
