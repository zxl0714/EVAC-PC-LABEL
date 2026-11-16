#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

class PS2Runtime;
namespace cave::ps2 {
class GsCommandBackend;
class HostPadSource;
struct NativeHostOptions {
    unsigned width=1280,height=720;
    bool fullscreen=false,vsync=false,timerPrecision=true;
    std::filesystem::path keyboardConfig,capture;
    uint64_t captureBytes=512ull*1024u*1024u;
    std::string gameId;
    bool gamepadEnabled=true;
    bool trackFieldBlits=false;
    bool reflex=true, amdAntiLag=false;
    float deadzone=0.20f,volume=1.0f;
    void* existingWindow{};
    void* closeEvent{};
    std::shared_ptr<HostPadSource> existingPad;
};
// Private implementation owns the native window, Vulkan consumer and SDL
// physical devices. Runtime/generated-code headers only forward-declare it.
class NativeHost {
public:
    NativeHost();
    ~NativeHost();
    void Configure(NativeHostOptions options);
    float Volume() const;
    void Initialize(const char* title);
    bool Alive();
    void OfferReadyFrame(PS2Runtime& runtime,uint64_t display2Buffer,unsigned leadUs);
    void BeforeInput(uint64_t responseField,unsigned budgetUs);
    void SimulationReady(uint64_t responseField);
    std::unique_ptr<GsCommandBackend> CreateBackend();
    void Run(PS2Runtime& runtime,const std::function<void()>& execute,
             const std::function<void()>& onFrame);
private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
