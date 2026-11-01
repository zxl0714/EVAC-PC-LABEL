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
    bool fullscreen=false,vsync=false;
    std::filesystem::path keyboardConfig,capture;
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
