#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/windowed_app_context.h>

namespace {
class TestContext final : public rex::ui::WindowedAppContext {
    void NotifyUILoopOfPendingFunctions() override {}
    void PlatformQuitFromUIThread() override {}
};
} // namespace

int main() {
    using rex::X_RESULT;
    wchar_t path[32768];
    auto length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path) || !SetEnvironmentVariableW(L"SDL3_DYNAMIC_API", path)) {
        return 1;
    }
    rex::InitLoggingEarly();
    rex::InitLogging(rex::BuildLogConfig(nullptr, "error", {}));
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    TestContext context;
    auto window = rex::ui::Window::Create(context, "Input test", 320, 240);
    rex::input::sdl::SDLInputDriver driver(nullptr, 0);
    driver.OnWindowAvailable(window.get());

    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons = 2;
    desc.naxes = 2;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_START);
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY);
    desc.name = "Guest input sampling fixture";
    auto id = SDL_AttachVirtualJoystick(&desc);
    auto* stick = id ? SDL_OpenJoystick(id) : nullptr;
    rex::input::X_INPUT_STATE state{};
    driver.GetState(0, &state); // Process device addition on the UI thread.
    auto slot = SDL_GetJoystickPlayerIndexForID(id);
    if (!stick || slot < 0 || slot >= HID_SDL_USER_COUNT) {
        std::fprintf(stderr, "Virtual gamepad was not assigned a slot: %s\n", SDL_GetError());
        return 1;
    }

    struct Input { bool pressed; Sint16 x; };
    const std::array<Input, 4> inputs{{{true, 0}, {false, 0}, {false, 16000}, {false, 0}}};
    std::array<double, 64> sample_us{};
    unsigned misses = 0, recovery_failures = 0;
    const auto matches = [&](Input input) {
        return (bool(state.gamepad.buttons & rex::input::X_INPUT_GAMEPAD_A) == input.pressed) &&
               state.gamepad.thumb_lx == input.x;
    };
    for (unsigned i = 0; i < sample_us.size(); ++i) {
        const auto input = inputs[i % inputs.size()];
        if (!SDL_SetJoystickVirtualButton(stick, 0, input.pressed) ||
            !SDL_SetJoystickVirtualAxis(stick, 0, input.x)) {
            return 1;
        }
        // The host state is ready; the UI has not yet serviced its next pump.
        X_RESULT result{};
        std::thread guest([&] {
            auto start = std::chrono::steady_clock::now();
            result = driver.GetState(slot, &state);
            sample_us[i] = std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - start).count();
        });
        guest.join();
        if (result != X_ERROR_SUCCESS || !matches(input)) ++misses;
        context.ExecutePendingFunctionsFromUIThread();
        if (driver.GetState(slot, &state) != X_ERROR_SUCCESS || !matches(input)) {
            ++recovery_failures;
        }
    }
    // Games use both the snapshot and keystroke APIs. A guest thread must
    // see the same freshly sampled, remapped Start through either API.
    unsigned keystroke_failures = 0;
    const auto read_stroke = [&] (rex::input::X_INPUT_KEYSTROKE& stroke) {
        X_RESULT result{};
        std::thread guest([&] { result = driver.GetKeystroke(slot, 0, &stroke); });
        guest.join();
        return result;
    };
    const auto expect_stroke = [&](const char* label, unsigned flags) {
        rex::input::X_INPUT_KEYSTROKE stroke{};
        const auto result = read_stroke(stroke);
        if (result != X_ERROR_SUCCESS ||
            stroke.virtual_key != uint16_t(rex::ui::VirtualKey::kXInputPadStart) ||
            stroke.flags != flags || stroke.user_index != slot) {
            ++keystroke_failures;
            std::fprintf(stderr, "%s: result=%u key=%04x flags=%u user=%u\n", label,
                         result, uint16_t(stroke.virtual_key), uint16_t(stroke.flags), stroke.user_index);
        }
    };
    const auto settle = [&] {
        context.ExecutePendingFunctionsFromUIThread();
        rex::input::X_INPUT_KEYSTROKE stroke{};
        for (unsigned i = 0; i < 40 && read_stroke(stroke) == X_ERROR_SUCCESS; ++i) {}
    };
    SDL_SetJoystickVirtualButton(stick, 1, true);
    expect_stroke("default Start down before UI pump", rex::input::X_INPUT_KEYSTROKE_KEYDOWN);
    settle();
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    SDL_SetJoystickVirtualButton(stick, 1, false);
    // A release must take precedence over a repeat that became due meanwhile.
    expect_stroke("Start release after repeat delay", rex::input::X_INPUT_KEYSTROKE_KEYUP);
    settle();
    rex::cvar::SetFlagByName("pad_start", "A");
    rex::cvar::SetFlagByName("pad_a", "NONE");
    SDL_SetJoystickVirtualButton(stick, 0, true);
    expect_stroke("remapped Start down", rex::input::X_INPUT_KEYSTROKE_KEYDOWN);
    settle();
    SDL_SetJoystickVirtualButton(stick, 0, false);
    expect_stroke("remapped Start up", rex::input::X_INPUT_KEYSTROKE_KEYUP);
    settle();
    rex::cvar::SetFlagByName("pad_start", "START");
    rex::cvar::SetFlagByName("pad_a", "A");
    SDL_CloseJoystick(stick);
    SDL_DetachVirtualJoystick(id);
    const bool removed = driver.GetState(slot, &state) == X_ERROR_DEVICE_NOT_CONNECTED;
    rex::ui::UIEvent closing(window.get());
    static_cast<rex::ui::WindowListener&>(driver).OnClosing(closing);
    std::sort(sample_us.begin(), sample_us.end());
    std::printf("guest samples=64 stale=%u after_ui_stale=%u detached=%d read_p50_us=%.1f read_p95_us=%.1f\n",
                misses, recovery_failures, removed, sample_us[32], sample_us[60]);
    std::printf("keystroke_failures=%u\n", keystroke_failures);
    return misses || recovery_failures || !removed || keystroke_failures;
}
