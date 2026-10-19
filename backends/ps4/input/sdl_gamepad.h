#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace kt::input {
struct GamepadSnapshot {
    std::uint32_t device{}, buttons{}, pending{};
    std::array<std::uint64_t, 32> edges{};
    std::uint8_t left_x=128, left_y=128, right_x=128, right_y=128, l2{}, r2{};
};
// Initialize/destroy on the host main thread. Poll on the existing pad sampler.
// SDL owns physical-device discovery only, never our window/keyboard/audio.
class SdlGamepad {
public:
    SdlGamepad();
    ~SdlGamepad();
    SdlGamepad(const SdlGamepad&) = delete;
    SdlGamepad& operator=(const SdlGamepad&) = delete;
    void Initialize();
    void Configure(bool enabled, double deadzone);
    void Focus(bool focused);
    void ClearPending();
    bool HasPending();
    GamepadSnapshot Poll();
    void Acknowledge(const GamepadSnapshot& delivered);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend bool SdlGamepadSelfTest();
    friend int SdlGamepadMonitor(unsigned seconds);
};
bool SdlGamepadSelfTest();
int SdlGamepadMonitor(unsigned seconds);
}
