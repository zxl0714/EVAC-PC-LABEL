#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class PS2Runtime;
namespace cave::ps2 {
class GsCommandBackend;
class HostPadSource;
struct GsPresentation {
    // Normalized destination of the full GS scanout in an aspect-fitted canvas.
    float canvasAspect{};
    float x{}, y{}, width{}, height{};
    unsigned quarterTurns{};
};
struct GsOverlay {
    struct Image { unsigned width{},height{}; std::vector<std::uint8_t> pixels; };
    struct Quad {
        unsigned image{};
        float x{},y{},width{},height{},fill=1;
        float verticalFill = 1;
    };
    std::vector<Image> images;
    std::function<void(std::vector<Quad>&)> quads;
};
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
    unsigned streamVolume=100,voiceVolume=100;
    void* existingWindow{};
    void* closeEvent{};
    std::shared_ptr<HostPadSource> existingPad;
    GsPresentation presentation;
    std::shared_ptr<GsOverlay> overlay;
};
// Private implementation owns the native window, Vulkan consumer and SDL
// physical devices. Runtime/generated-code headers only forward-declare it.
class NativeHost {
public:
    NativeHost();
    ~NativeHost();
    void Configure(NativeHostOptions options);
    float Volume() const;
    unsigned StreamVolume() const;
    unsigned VoiceVolume() const;
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
