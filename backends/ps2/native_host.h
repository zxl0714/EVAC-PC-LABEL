#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

class PS2Runtime;
namespace cave::ps2 {
class GsCommandBackend;
struct NativeHostOptions {
    unsigned width=1280,height=720;
    bool fullscreen=false,vsync=false,timerPrecision=true;
    std::filesystem::path keyboardConfig,capture;
    std::string gameId;
    bool gamepadEnabled=true;
    float deadzone=0.20f,volume=1.0f;
};
// Private implementation owns the native window, Vulkan consumer and SDL
// physical devices. Runtime/generated-code headers only forward-declare it.
class NativeHost {
public:
    NativeHost();
    ~NativeHost();
    void Configure(NativeHostOptions options);
    void Initialize(const char* title);
    std::unique_ptr<GsCommandBackend> CreateBackend();
    void Run(PS2Runtime& runtime,const std::function<void()>& execute,
             const std::function<void()>& onFrame);
private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
