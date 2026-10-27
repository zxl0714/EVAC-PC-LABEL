#include "native_pad.h"
#include "input/keyboard_config.h"
#include "input/sdl_gamepad.h"
#include "native_diagnostics.h"
#include "native_time.h"
#include "native_user.h"
#include "native_video.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
constexpr int kArgument = static_cast<int>(0x80920001u);
constexpr int kPort = static_cast<int>(0x80920002u);
constexpr int kHandle = static_cast<int>(0x80920003u);
constexpr int kOpened = static_cast<int>(0x80920004u);
constexpr int kNotInitialized = static_cast<int>(0x80920005u);
constexpr int kFatal = static_cast<int>(0x809200ffu);
struct PadData {
    std::uint32_t buttons{};
    std::uint8_t left_x = 128, left_y = 128, right_x = 128, right_y = 128;
    std::uint8_t l2{}, r2{}, analog_padding[2]{};
    float orientation[4]{0, 0, 0, 1}, acceleration[3]{}, angular_velocity[3]{};
    std::uint8_t touch[24]{};
    std::uint8_t connected{}, alignment[3]{};
    std::uint64_t timestamp{};
    std::uint8_t extension[16]{}, connected_count{}, reserved[2]{}, unique_size{}, unique[12]{};
};
static_assert(sizeof(PadData) == 120 && offsetof(PadData, connected) == 76 &&
              offsetof(PadData, timestamp) == 80 && offsetof(PadData, connected_count) == 104);
struct ControllerInformation {
    float touch_density{};
    std::uint16_t touch_width{}, touch_height{};
    std::uint8_t dead_zone_left{}, dead_zone_right{}, connection_type{}, connected_count{}, connected{}, padding[3]{};
    std::int32_t device_class{};
    std::uint8_t reserved[8]{};
};
static_assert(sizeof(ControllerInformation) == 28 && offsetof(ControllerInformation, device_class) == 16);

struct Keyboard {
    kt::input::KeyboardBindings bindings = kt::input::DefaultKeyboardBindings();
    void* owner{};
    bool focused{};
    std::array<bool, 256> keys{};
    // Preserve a real down edge until the game consumes a report, even when
    // Windows delivers down/up between two 60 Hz samples. Held keys still use
    // their physical state; focus/port loss drops unconsumed edges.
    std::uint32_t pending{};
    unsigned diagnostic_tap_ms{};
    unsigned diagnostic_circle_hold_ms{};
    std::uint32_t diagnostic_hold_button{0x2000};
    std::array<std::uint64_t, 256> pulse_until{};
    std::uint32_t mouse_left_binding{};
    std::uint32_t Binding(unsigned key) const {
        if (key == VK_LBUTTON) { return mouse_left_binding; }
        if (key == 'H' && diagnostic_circle_hold_ms) { return diagnostic_hold_button; }
        return key < bindings.size() ? bindings[key] : 0;
    }
    void Focus(void* window, bool active) {
        if (owner != window) { return; }
        focused = active;
        if (!active) { keys.fill(false); pulse_until.fill(0); pending = 0; }
    }
    void Key(void* window, unsigned key, bool down, std::uint64_t now = GetTickCount64()) {
        if (window == owner && focused && key < keys.size()) {
            if (down && !keys[key]) {
                pending |= Binding(key);
                // The explicit confirmation diagnostic also supports one
                // bounded firing hold. Normal H retains its short confirmation
                // duration; Shift+H is useful when CU can only send tap edges.
                // This never exists in ordinary input (diagnostic default 0).
                const bool circle = key=='H' && diagnostic_circle_hold_ms;
                const bool shift = keys[VK_SHIFT] || keys[VK_LSHIFT] || keys[VK_RSHIFT];
                const auto duration=circle ? (shift ? 30000u : diagnostic_circle_hold_ms) : diagnostic_tap_ms;
                if (duration) { pulse_until[key] = now + duration; }
            }
            keys[key] = down;
        }
    }
    void Acknowledge() { pending = 0; }
    std::uint32_t Buttons(std::uint64_t now = GetTickCount64()) const {
        if (!focused) { return 0; }
        std::uint32_t result = pending;
        for (unsigned key = 0; key < keys.size(); ++key) {
            if (keys[key] || pulse_until[key] > now) { result |= Binding(key); }
        }
        return result;
    }
};
void Translate(PadData& output, const kt::input::GamepadSnapshot& input) {
    output.buttons |= input.buttons;
    output.l2=input.l2; output.r2=input.r2;
    output.left_x=input.left_x; output.left_y=input.left_y;
    output.right_x=input.right_x; output.right_y=input.right_y;
}
struct History {
    std::array<PadData, 64> samples{};
    std::size_t first{}, count{};
    PadData latest{};
    void Push(const PadData& value) {
        latest = value;
        if (count == samples.size()) { first = (first + 1) % samples.size(); --count; }
        samples[(first + count) % samples.size()] = value; ++count;
    }
    int Read(PadData* output, int capacity) {
        // Single-report polling must not chase up to a second of retained input.
        // Also retire history so a later multi-report read cannot replay it.
        if (capacity == 1) { *output = latest; first = count = 0; return 1; }
        const auto number = (std::min)(count, static_cast<std::size_t>(capacity));
        for (std::size_t i = 0; i < number; ++i) { output[i] = samples[(first + i) % samples.size()]; }
        first = (first + number) % samples.size(); count -= number;
        return static_cast<int>(number);
    }
};
struct PadState {
    const DWORD host_main_thread=GetCurrentThreadId();
    kt::input::KeyboardBindings keyboard_bindings = kt::input::DefaultKeyboardBindings();
    std::mutex mutex;
    std::condition_variable condition;
    Keyboard keyboard;
    History history;
    kt::input::SdlGamepad gamepad;
    kt::input::GamepadSnapshot physical;
    bool synthetic_input{};
    std::thread sampler;
    bool initialized{}, stopping{};
    int handle{}, next = 1;
    int source = -2;
    std::uint32_t source_device{};
    std::uint8_t connections{};
    unsigned logged_window_edges{}, logged_read_changes{};
    std::uint32_t last_read_buttons{};
    std::uint32_t mouse_left_binding{};
    unsigned diagnostic_tap_ms{};
    unsigned diagnostic_circle_hold_ms{};
    std::uint32_t diagnostic_hold_button{0x2000};
    ~PadState() {
        { std::lock_guard lock(mutex); stopping = true; condition.notify_all(); }
        if (sampler.joinable()) { sampler.join(); }
    }
    void Sample() { // mutex held; never calls the window or its state mutex
        PadData output{};
        int selected = keyboard.owner ? -1 : -2;
        if (!synthetic_input) physical=gamepad.Poll();
        if (physical.device) selected=-3;
        output.connected = selected != -2;
        if (selected != source || physical.device!=source_device) {
            if (output.connected && connections != 255) { ++connections; }
            source = selected;
            source_device=physical.device;
            std::printf("{\"event\":\"native_pad_source\",\"backend\":\"%s\",\"sdl_instance\":%u,\"connected\":%s}\n", selected==-3?"sdl":"keyboard", physical.device, output.connected ? "true" : "false");
        }
        output.connected_count = connections;
        // Both backends respect this runtime's focus. No global keyboard state.
        if (keyboard.focused) {
            if (physical.device) { Translate(output, physical); }
            const auto keys = keyboard.Buttons();
            output.buttons |= keys;
            if (keys & 0x100) { output.l2 = 255; }
            if (keys & 0x200) { output.r2 = 255; }
        }
        output.timestamp = NativeProcessMicroseconds();
        history.Push(output);
    }
};
// Constructed before main: function-local video/window state is destroyed first.
// The sampler is joined while this object's lock, clock and DLL are still alive.
PadState pad_state;
PadState& State() { return pad_state; }
void SetKeyboardBindings(const kt::input::KeyboardBindings& bindings) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (state.initialized || state.keyboard.owner) { throw std::runtime_error("Input config must be loaded before the pad/window starts"); }
    if (state.diagnostic_circle_hold_ms && bindings['H']) { throw std::runtime_error("Input config key H conflicts with diagnostic Circle hold"); }
    state.keyboard_bindings = bindings;
}
int Initialize(const void* caller) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    if (state.initialized) { return 0; }
    if (GetCurrentThreadId()!=state.host_main_thread) { return kFatal; }
    try {
        if (!state.synthetic_input) state.gamepad.Initialize();
        state.Sample();
        state.sampler = std::thread([&state] {
            std::unique_lock gate(state.mutex);
            auto next = std::chrono::steady_clock::now();
            while (!state.stopping) {
                next += std::chrono::nanoseconds(16666667);
                if (state.condition.wait_until(gate, next, [&] { return state.stopping; })) { break; }
                state.Sample();
                if (next < std::chrono::steady_clock::now()) { next = std::chrono::steady_clock::now(); }
            }
        });
    } catch (const std::exception& error) {
        std::fprintf(stderr,"Pad initialization: %s\n",error.what()); return kFatal;
    } catch (...) {
        return kFatal;
    }
    state.initialized = true;
    TraceNativeApi("scePadInit", 0, caller);
    return 0;
}
int Open(int user, int type, int index, const void* parameter, const void* caller) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (!state.initialized) { return kNotInitialized; }
    if (user == -1 || user == 0xff) { return static_cast<int>(0x809b0001u); }
    if (user != 0x1000 || !NativeUserInitialized()) { return static_cast<int>(0x809b0081u); }
    if (type != 0 || index != 0) { return kPort; }
    if (parameter) { UnsupportedApi("scePadOpen_parameter", reinterpret_cast<std::uintptr_t>(parameter), caller); }
    if (state.handle) { return kOpened; }
    if (state.next == (std::numeric_limits<int>::max)()) { return kFatal; }
    state.keyboard.Acknowledge(); state.gamepad.ClearPending(); state.history = {}; state.Sample(); state.handle = state.next++;
    TraceNativeApi("scePadOpen", static_cast<unsigned>(state.handle), caller);
    return state.handle;
}
int Close(int handle) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (!state.initialized) { return kNotInitialized; }
    if (!handle || handle != state.handle) { return kHandle; }
    state.keyboard.Acknowledge(); state.gamepad.ClearPending(); state.handle = 0; state.history = {}; return 0;
}
int ReadReports(PadState& state, PadData* output, int count, bool latest) { // mutex held
    const bool edge = state.keyboard.pending != 0 || state.gamepad.HasPending();
    if (edge) { state.Sample(); }
    const auto pending=state.keyboard.pending | state.physical.pending;
    const auto reports = state.history.Read(output, latest ? 1 : count);
    // A bounded read may stop before the newly sampled edge (e.g. 64 queued
    // reports with the game's capacity of 63). Retire the latch only after a
    // returned report actually contains every pending button.
    if (reports && edge &&
        (output[reports - 1].buttons & pending) == pending) {
        state.keyboard.Acknowledge();
        state.gamepad.Acknowledge(state.physical);
        // Publish release/continued hold now, so the next single-state read
        // cannot replay the latch left in the sampler's latest report.
        state.Sample();
    }
    if (reports && output[reports - 1].buttons != state.last_read_buttons) {
        state.last_read_buttons = output[reports - 1].buttons;
        if (state.logged_read_changes < 64) {
            ++state.logged_read_changes;
            std::printf("{\"event\":\"native_pad_read_buttons\",\"buttons\":%u,\"reports\":%d,\"timestamp_us\":%llu}\n",
                output[reports - 1].buttons, reports, output[reports - 1].timestamp);
        }
    }
    return latest ? 0 : reports;
}
int Read(int handle, PadData* output, int count, bool latest) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (!state.initialized) { return kNotInitialized; }
    if (!handle || handle != state.handle) { return kHandle; }
    if (!output || count < 1 || count > 64) { return kArgument; }
    return ReadReports(state, output, count, latest);
}
int Information(int handle, ControllerInformation* output) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (!state.initialized) { return kNotInitialized; }
    if (!handle || handle != state.handle) { return kHandle; }
    if (!output) { return kArgument; }
    ControllerInformation value{};
    value.connected = state.history.latest.connected;
    value.connected_count = state.history.latest.connected_count;
    *output = value; return 0;
}
int KT_SYSV PadInit() { return Initialize(__builtin_return_address(0)); }
int KT_SYSV PadOpen(int user, int type, int index, const void* parameter) { return Open(user, type, index, parameter, __builtin_return_address(0)); }
int KT_SYSV PadClose(int handle) { return Close(handle); }
int KT_SYSV PadRead(int handle, PadData* output, int count) { return Read(handle, output, count, false); }
int KT_SYSV PadReadState(int handle, PadData* output) { return Read(handle, output, 1, true); }
int KT_SYSV PadInformation(int handle, ControllerInformation* output) { return Information(handle, output); }
}

void NativePadAttachWindow(void* window) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (!window || state.keyboard.owner) { UnsupportedApi("native_pad_window_owner", reinterpret_cast<std::uintptr_t>(window), nullptr); }
    state.keyboard = {}; state.keyboard.owner = window;
    state.gamepad.Focus(false);
    state.keyboard.bindings = state.keyboard_bindings;
    state.keyboard.mouse_left_binding = state.mouse_left_binding;
    state.keyboard.diagnostic_tap_ms = state.diagnostic_tap_ms;
    state.keyboard.diagnostic_circle_hold_ms = state.diagnostic_circle_hold_ms;
    state.keyboard.diagnostic_hold_button = state.diagnostic_hold_button;
}
void NativePadDetachWindow(void* window) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (state.keyboard.owner == window) {
        state.keyboard = {}; state.history = {};
        state.gamepad.Focus(false);
        if (state.initialized) { state.Sample(); }
    }
}
void NativePadWindowFocus(void* window, bool focused) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (state.keyboard.owner != window) { return; }
    state.keyboard.Focus(window, focused);
    state.gamepad.Focus(focused);
    if (!focused) {
        state.history = {};
        if (state.initialized) { state.Sample(); }
    }
}
void NativePadWindowKey(void* window, unsigned key, bool down) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    const auto binding = state.keyboard.Binding(key);
    const bool changed = window == state.keyboard.owner && state.keyboard.focused && key < state.keyboard.keys.size() &&
        state.keyboard.keys[key] != down;
    state.keyboard.Key(window, key, down);
    if (binding && changed && state.logged_window_edges < 64) {
        ++state.logged_window_edges;
        std::printf("{\"event\":\"native_pad_window_edge\",\"source\":\"%s\",\"binding\":%u,\"down\":%s,\"timestamp_us\":%llu}\n",
            key == VK_LBUTTON ? "mouse_left" : "keyboard", binding, down ? "true" : "false", NativeProcessMicroseconds());
    }
}
bool ConfigureNativePadMouseLeft(std::wstring_view button) {
    constexpr std::pair<std::wstring_view, std::uint32_t> bindings[] = {
        {L"none", 0}, {L"cross", 0x4000}, {L"circle", 0x2000}, {L"square", 0x8000},
        {L"triangle", 0x1000}, {L"options", 8}, {L"touchpad", 0x100000}};
    for (const auto& [name, mask] : bindings) {
        if (button != name) { continue; }
        auto& state = State(); std::lock_guard lock(state.mutex);
        if (state.initialized || state.keyboard.owner) { return false; }
        state.mouse_left_binding = mask;
        std::printf("{\"event\":\"native_pad_input_binding\",\"source\":\"mouse_left\",\"binding\":%u,\"physical_edges_only\":true}\n", mask);
        return true;
    }
    return false;
}
bool NativePadMouseLeftMapped() {
    auto& state = State(); std::lock_guard lock(state.mutex);
    return state.mouse_left_binding != 0;
}
void ConfigureNativePadKeyboard(const std::filesystem::path& path) {
    const auto bindings = kt::input::LoadKeyboardConfig(path);
    SetKeyboardBindings(bindings);
    std::puts("{\"event\":\"native_pad_keyboard_config\",\"loaded\":true,\"restart_required\":true}");
    for (unsigned key = 0; key < bindings.size(); ++key) {
        if (bindings[key]) { std::printf("{\"event\":\"native_pad_keyboard_binding\",\"virtual_key\":%u,\"button\":%u}\n", key, bindings[key]); }
    }
}
void ConfigureNativePadSettings(const kt::input::KeyboardBindings& bindings, bool gamepad, double deadzone) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (state.initialized || state.keyboard.owner) { throw std::runtime_error("Input config must precede pad/window initialization"); }
    if (state.diagnostic_circle_hold_ms && bindings['H']) { throw std::runtime_error("Input config key H conflicts with diagnostic Circle hold"); }
    state.gamepad.Configure(gamepad, deadzone);
    state.keyboard_bindings = bindings;
    std::printf("{\"event\":\"native_pad_collection_settings\",\"gamepad\":%s,\"deadzone\":%.6f}\n", gamepad ? "true" : "false", deadzone);
}
bool ConfigureNativeDiagnosticTaps(unsigned milliseconds) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (milliseconds > 500 || state.initialized || state.keyboard.owner) { return false; }
    state.diagnostic_tap_ms = milliseconds;
    std::printf("{\"event\":\"native_pad_diagnostic_taps\",\"milliseconds\":%u,\"changes_game_coin_counter\":false}\n", milliseconds);
    return true;
}
static bool ConfigureNativeDiagnosticHold(unsigned milliseconds, bool cross) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (milliseconds > 2000 || state.initialized || state.keyboard.owner || (milliseconds && state.keyboard_bindings['H'])) { return false; }
    state.diagnostic_circle_hold_ms = milliseconds;
    state.diagnostic_hold_button = cross ? 0x4000 : 0x2000;
    std::printf("{\"event\":\"native_pad_diagnostic_%s_hold\",\"trigger\":\"H\",\"milliseconds\":%u,\"shift_hold_ms\":%u,\"changes_game_state_directly\":false}\n", cross ? "cross" : "circle", milliseconds, milliseconds ? 30000u : 0u);
    return true;
}
bool ConfigureNativeDiagnosticCircleHold(unsigned milliseconds) { return ConfigureNativeDiagnosticHold(milliseconds,false); }
bool ConfigureNativeDiagnosticCrossHold(unsigned milliseconds) { return ConfigureNativeDiagnosticHold(milliseconds,true); }
std::uint64_t ResolvePadImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libScePad" || module != "libScePad") { return 0; }
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "hv1luiJrqQM") { return reinterpret_cast<std::uint64_t>(&PadInit); }
    if (id == "xk0AcarP3V4") { return reinterpret_cast<std::uint64_t>(&PadOpen); }
    if (id == "6ncge5+l5Qs") { return reinterpret_cast<std::uint64_t>(&PadClose); }
    if (id == "q1cHNfGycLI") { return reinterpret_cast<std::uint64_t>(&PadRead); }
    if (id == "YndgXqQVV7c") { return reinterpret_cast<std::uint64_t>(&PadReadState); }
    if (id == "gjP9-KQzoUk") { return reinterpret_cast<std::uint64_t>(&PadInformation); }
    return 0;
}
bool PadSelfTest() {
    // Deterministic ABI/history checks must not depend on plugged-in hardware.
    State().synthetic_input=true;
    int passed = 0; bool okay = true;
    const auto check = [&](bool result, const char* name) {
        std::printf("{\"event\":\"pad_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
        okay &= result; if (result) { ++passed; }
    };
    check(Read(1, nullptr, 1, false) == kNotInitialized, "before_initialization");
    Keyboard keyboard; int owner{}, other{}; keyboard.owner = &owner;
    keyboard.Key(&owner, 'Z', true); keyboard.Focus(&owner, true); keyboard.Key(&other, 'X', true);
    check(keyboard.Buttons() == 0, "owned_focused_input_only");
    keyboard.Key(&owner, 'Z', true); keyboard.Key(&owner, VK_UP, true);
    check(keyboard.Buttons() == 0x4010, "keyboard_bindings");
    keyboard.Focus(&owner, false); keyboard.Focus(&owner, true);
    Keyboard mouse; mouse.owner = &owner; mouse.Focus(&owner, true);
    mouse.Key(&owner, VK_LBUTTON, true); mouse.Key(&owner, VK_LBUTTON, false);
    check(!mouse.Buttons() && !mouse.pending, "mouse_left_disabled_by_default");
    mouse.mouse_left_binding = 0x1000;
    mouse.Key(&owner, VK_LBUTTON, true); mouse.Acknowledge();
    const bool mouse_held = mouse.Buttons() == 0x1000 && !mouse.pending;
    mouse.Key(&owner, VK_LBUTTON, false);
    check(mouse_held && !mouse.Buttons(), "mouse_mapping_preserves_real_hold_and_release");
    mouse.Key(&other, VK_LBUTTON, true);
    const bool foreign_dropped = !mouse.Buttons();
    mouse.Key(&owner, VK_LBUTTON, true); mouse.Focus(&owner, false); mouse.Focus(&owner, true);
    check(foreign_dropped && !mouse.Buttons() && !mouse.pending, "mouse_focus_and_foreign_window_guard");
    check(keyboard.Buttons() == 0, "focus_loss_releases_keys");
    Keyboard diagnostic; diagnostic.owner = &owner; diagnostic.Focus(&owner, true); diagnostic.diagnostic_tap_ms = 120;
    diagnostic.Key(&owner, 'V', true, 1000); diagnostic.Key(&owner, 'V', false, 1002); diagnostic.Acknowledge();
    check(diagnostic.Buttons(1119) == 0x1000 && diagnostic.Buttons(1120) == 0, "diagnostic_taps_have_explicit_wall_clock_duration_not_frame_count");
    diagnostic.Key(&owner, 'H', true, 1130); diagnostic.Key(&owner, 'H', false, 1132); diagnostic.Acknowledge();
    const bool hold_disabled=diagnostic.Buttons(1133)==0;
    diagnostic.diagnostic_circle_hold_ms=1500;
    diagnostic.Key(&owner, 'H', true, 1200); diagnostic.Key(&owner, 'H', false, 1202);
    diagnostic.Key(&owner, VK_RIGHT, true, 1200); diagnostic.Key(&owner, VK_RIGHT, false, 1202); diagnostic.Acknowledge();
    check(hold_disabled && diagnostic.Buttons(1319)==0x2020 && diagnostic.Buttons(1320)==0x2000 &&
        diagnostic.Buttons(2699)==0x2000 && diagnostic.Buttons(2700)==0, "diagnostic_circle_hold_is_opt_in_and_does_not_lengthen_navigation");
    diagnostic.Key(&owner, VK_LSHIFT, true, 3000); diagnostic.Key(&owner, 'H', true, 3010);
    diagnostic.Key(&owner, 'H', false, 3012); diagnostic.Key(&owner, VK_LSHIFT, false, 3013); diagnostic.Acknowledge();
    check(diagnostic.Buttons(33009)==0x2000 && diagnostic.Buttons(33010)==0,
        "diagnostic_shift_circle_hold_has_a_bounded_wall_clock_deadline");
    diagnostic.Key(&owner, 'H', true, 34000); diagnostic.Key(&owner, 'H', false, 34002); diagnostic.Acknowledge();
    check(diagnostic.Buttons(35499)==0x2000 && diagnostic.Buttons(35500)==0,
        "ordinary_diagnostic_confirmation_keeps_its_short_duration");
    diagnostic.Key(&owner, VK_SHIFT, true, 36000); diagnostic.Key(&owner, 'H', true, 36010);
    diagnostic.Key(&owner, 'H', false, 36012); diagnostic.Key(&owner, VK_SHIFT, false, 36013); diagnostic.Acknowledge();
    diagnostic.Key(&owner, 'H', true, 36100); diagnostic.Key(&owner, 'H', false, 36102); diagnostic.Acknowledge();
    check(diagnostic.Buttons(37599)==0x2000 && diagnostic.Buttons(37600)==0,
        "ordinary_h_can_shorten_an_existing_diagnostic_long_hold");
    diagnostic.Key(&owner, VK_RSHIFT, true, 38000); diagnostic.Key(&owner, 'H', true, 38010);
    diagnostic.Focus(&owner, false); diagnostic.Focus(&owner, true);
    check(diagnostic.Buttons(38011)==0, "focus_loss_clears_the_diagnostic_long_hold_and_modifiers");
    Keyboard ordinary; ordinary.owner=&owner; ordinary.Focus(&owner,true);
    ordinary.Key(&owner,VK_SHIFT,true,40000); ordinary.Key(&owner,'H',true,40001);
    ordinary.Key(&owner,'H',false,40002); ordinary.Key(&owner,VK_SHIFT,false,40003); ordinary.Acknowledge();
    check(ordinary.Buttons(40004)==0, "shift_h_remains_unbound_when_circle_diagnostic_is_disabled");
    Keyboard cross; cross.owner=&owner; cross.Focus(&owner,true);
    cross.diagnostic_hold_button=0x4000;
    cross.Key(&owner,'H',true,41000); cross.Key(&owner,'H',false,41002); cross.Acknowledge();
    check(cross.Buttons(41003)==0, "cross_diagnostic_button_alone_does_not_enable_hold");
    cross.diagnostic_circle_hold_ms=1500;
    cross.Key(&owner,'H',true,42000); cross.Key(&owner,'H',false,42002); cross.Acknowledge();
    check(cross.Buttons(43499)==0x4000 && cross.Buttons(43500)==0,
        "cross_diagnostic_uses_cross_only_and_same_bounded_deadline");
    cross.Key(&owner,VK_SHIFT,true,44000); cross.Key(&owner,'H',true,44010);
    cross.Key(&owner,'H',false,44012); cross.Key(&owner,VK_SHIFT,false,44013); cross.Acknowledge();
    check(cross.Buttons(74009)==0x4000 && cross.Buttons(74010)==0,
        "cross_diagnostic_long_hold_uses_same_deadline");
    cross.Key(&owner,'H',true,75000); cross.Focus(&owner,false); cross.Focus(&owner,true);
    check(cross.Buttons(75001)==0, "cross_diagnostic_clears_on_focus_loss");
    diagnostic.Key(&owner, 'V', true, 1200); diagnostic.Focus(&owner, false); diagnostic.Focus(&owner, true);
    check(diagnostic.Buttons(1201) == 0, "diagnostic_taps_cannot_survive_focus_loss");
    keyboard.Key(&owner, VK_RETURN, true); keyboard.Key(&owner, VK_RETURN, false);
    check(keyboard.Buttons() == 8, "short_keyboard_press_survives_between_samples");
    keyboard.Acknowledge();
    check(keyboard.Buttons() == 0, "acknowledge_releases_completed_tap");
    keyboard.Key(&owner, 'Z', true); keyboard.Acknowledge(); keyboard.Key(&owner, 'Z', true);
    check(keyboard.Buttons() == 0x4000 && keyboard.pending == 0, "acknowledge_preserves_hold_without_repeat_edge");
    keyboard.Key(&owner, 'Z', false); keyboard.Key(&other, 'X', true); keyboard.Key(&other, 'X', false);
    keyboard.Key(&owner, VK_RETURN, true); keyboard.Key(&owner, VK_RETURN, false);
    keyboard.Focus(&owner, false); keyboard.Focus(&owner, true);
    check(keyboard.Buttons() == 0 && keyboard.pending == 0, "focus_loss_and_other_windows_cannot_replay_edges");
    keyboard.Focus(&owner, false); keyboard.Focus(&owner, true);
    kt::input::GamepadSnapshot input{}; input.buttons=0x104900;
    input.l2=31; input.left_x=0; input.left_y=0;
    PadData data; Translate(data, input);
    check(data.buttons == 0x104900 && data.left_x == 0 && data.left_y == 0 && data.l2 == 31 &&
          data.right_x==128 && data.right_y==128, "sdl_snapshot_to_ps4_report");
    History history;
    for (std::uint64_t i = 0; i < 80; ++i) { data.timestamp = i; history.Push(data); }
    std::array<PadData, 64> output{};
    check(history.Read(output.data(), 64) == 64 && output.front().timestamp == 16 && output.back().timestamp == 79 &&
          history.latest.timestamp == 79 && history.Read(output.data(), 2) == 0, "bounded_history_order_and_consumption");
    history.Push(data); data.timestamp = 90; history.Push(data);
    check(history.Read(output.data(), 1) == 1 && output[0].timestamp == 90 && history.Read(output.data(), 2) == 0,
          "single_poll_discards_old_history");
    check(ConfigureNativePadMouseLeft(L"triangle") && NativePadMouseLeftMapped() &&
          !ConfigureNativePadMouseLeft(L"not-a-button") && NativePadMouseLeftMapped() &&
          ConfigureNativePadMouseLeft(L"none") && !NativePadMouseLeftMapped() && ConfigureNativePadMouseLeft(L"triangle"),
          "mouse_configuration_validation_and_no_invalid_mutation");
    const auto custom = kt::input::ParseKeyboardConfig("[keyboard]\nsquare = Space, B\n");
    check(custom[VK_SPACE] == 0x8000 && custom['B'] == 0x8000 && !custom['C'] && custom['Z'] == 0x4000,
          "external_config_replaces_keys_and_retains_unlisted_defaults");
    Keyboard configured; configured.bindings = custom; configured.owner = &owner; configured.Focus(&owner, true);
    configured.Key(&owner, 'C', true); configured.Acknowledge();
    const bool old_key_removed = !configured.Buttons();
    configured.Key(&owner, VK_SPACE, true); configured.Key(&owner, 'B', true); configured.Acknowledge();
    configured.Key(&owner, VK_SPACE, false);
    const bool alias_held = configured.Buttons() == 0x8000;
    configured.Key(&owner, 'B', false);
    check(old_key_removed && alias_held && !configured.Buttons(), "configured_aliases_release_independently");
    configured.Key(&owner, 'B', true); configured.Focus(&owner, false); configured.Focus(&owner, true);
    check(!configured.Buttons() && !configured.pending, "configured_keys_clear_on_focus_loss");
    SetKeyboardBindings(custom);
    check(Initialize(nullptr) == 0 && Initialize(nullptr) == 0 && Read(0, nullptr, 0, false) == kHandle,
          "keyboard_backend_and_idempotence");
    auto& state = State();
    std::uint64_t before{};
    { std::lock_guard lock(state.mutex); before = state.history.latest.timestamp; }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    { std::lock_guard lock(state.mutex); check(state.history.latest.timestamp > before, "real_background_sampling"); }
    using UserInitialize = int (KT_SYSV *)(const void*);
    const auto user_init = reinterpret_cast<UserInitialize>(ResolveUserImport("j3YMu1MVNNo", "libSceUserService", "libSceUserService"));
    const int user_ready = user_init(nullptr);
    const auto handle = Open(0x1000, 0, 0, nullptr, nullptr);
    ControllerInformation information{};
    check(user_ready == 0 && handle > 0 && Open(0x1000, 0, 0, nullptr, nullptr) == kOpened &&
          Read(handle, nullptr, 1, false) == kArgument && Read(handle, output.data(), 65, false) == kArgument &&
          Read(handle, output.data(), 1, true) == 0 && output[0].orientation[3] == 1 &&
          Information(handle, &information) == 0 && Close(handle) == 0 && Close(handle) == kHandle,
          "open_read_information_and_handle_lifetime");
    const auto edge_handle = Open(0x1000, 0, 0, nullptr, nullptr);
    NativePadAttachWindow(&owner); NativePadWindowFocus(&owner, true);
    check(!ConfigureNativePadMouseLeft(L"none") && NativePadMouseLeftMapped(), "late_mouse_reconfiguration_rejected");
    NativePadWindowKey(&owner, VK_RETURN, true); NativePadWindowKey(&owner, VK_RETURN, false);
    const auto invalid_read = Read(edge_handle, nullptr, 1, true);
    { std::lock_guard lock(state.mutex); check(invalid_read == kArgument && state.keyboard.pending == 8,
        "invalid_report_does_not_consume_keyboard_edge"); }
    const auto edge_read = Read(edge_handle, output.data(), 1, true);
    { std::lock_guard lock(state.mutex); check(edge_read == 0 && (output[0].buttons & 8) && !state.keyboard.pending && !state.keyboard.Buttons(),
        "single_report_delivers_and_acknowledges_short_press"); }
    NativePadWindowKey(&owner, 'X', true); NativePadWindowKey(&owner, 'X', false);
    const auto multi_read = Read(edge_handle, output.data(), 64, false);
    { std::lock_guard lock(state.mutex); check(multi_read > 0 && (output[multi_read - 1].buttons & 0x2000) && !state.keyboard.pending,
        "multi_report_read_includes_and_acknowledges_short_press"); }
    for (const int capacity : {2, 63}) {
        std::lock_guard lock(state.mutex);
        state.keyboard.Focus(&owner, false); state.keyboard.Focus(&owner, true);
        state.history = {};
        PadData neutral{}; neutral.connected = 1;
        for (int i = 0; i < 64; ++i) { state.history.Push(neutral); }
        state.keyboard.Key(&owner, 'V', true); state.keyboard.Key(&owner, 'V', false);
        const auto first_read = ReadReports(state, output.data(), capacity, false);
        bool retained = first_read == capacity && !(output[first_read - 1].buttons & 0x1000) &&
                        state.keyboard.pending == 0x1000;
        int delivered = 0;
        for (int attempt = 0; state.keyboard.pending && attempt < 64; ++attempt) {
            const auto number = ReadReports(state, output.data(), capacity, false);
            if (number && (output[number - 1].buttons & 0x1000) && !state.keyboard.pending) { ++delivered; }
        }
        const bool acknowledged = !state.keyboard.pending;
        ReadReports(state, output.data(), 1, true);
        check(retained && delivered == 1 && acknowledged && !(output[0].buttons & 0x1000),
              capacity == 63 ? "full_history_capacity63_does_not_acknowledge_undelivered_edge" :
                               "full_history_capacity2_retains_edge_until_returned");
    }
    NativePadWindowKey(&owner, VK_LBUTTON, true);
    Read(edge_handle, output.data(), 1, true);
    const bool first_mouse_read = (output[0].buttons & 0x1000) != 0;
    Read(edge_handle, output.data(), 1, true);
    const bool held_mouse_read = (output[0].buttons & 0x1000) != 0;
    NativePadWindowKey(&owner, VK_LBUTTON, false);
    { std::lock_guard lock(state.mutex); state.Sample(); }
    Read(edge_handle, output.data(), 1, true);
    check(first_mouse_read && held_mouse_read && !(output[0].buttons & 0x1000), "physical_mouse_binding_in_real_pad_reports");
    NativePadWindowKey(&owner, VK_SPACE, true); NativePadWindowKey(&owner, 'B', true);
    Read(edge_handle, output.data(), 1, true);
    const bool configured_first = (output[0].buttons & 0x8000) != 0;
    NativePadWindowKey(&owner, VK_SPACE, false);
    { std::lock_guard lock(state.mutex); state.Sample(); }
    Read(edge_handle, output.data(), 1, true);
    const bool configured_still_held = (output[0].buttons & 0x8000) != 0;
    NativePadWindowKey(&owner, 'B', false); NativePadWindowKey(&owner, 'C', true);
    { std::lock_guard lock(state.mutex); state.Sample(); }
    Read(edge_handle, output.data(), 1, true);
    check(configured_first && configured_still_held && !(output[0].buttons & 0x8000), "configured_mapping_reaches_real_pad_reports");
    NativePadDetachWindow(&owner); NativePadAttachWindow(&owner); NativePadWindowFocus(&owner, true);
    NativePadWindowKey(&owner, VK_SPACE, true); NativePadWindowKey(&owner, VK_SPACE, false);
    Read(edge_handle, output.data(), 1, true);
    bool late_rejected = false;
    try { SetKeyboardBindings(kt::input::DefaultKeyboardBindings()); } catch (const std::runtime_error&) { late_rejected = true; }
    check(late_rejected && (output[0].buttons & 0x8000), "configured_mapping_survives_window_rebind_and_rejects_late_change");
    NativePadDetachWindow(&owner); Close(edge_handle);
    std::printf("{\"event\":\"pad_self_test\",\"passed\":%d,\"success\":%s}\n", passed, okay ? "true" : "false");
    return okay;
}

int PadDemo() {
    using UserInitialize = int (KT_SYSV *)(const void*);
    const auto user_init = reinterpret_cast<UserInitialize>(ResolveUserImport("j3YMu1MVNNo", "libSceUserService", "libSceUserService"));
    if (user_init(nullptr) != 0 || Initialize(nullptr) != 0) { return 1; }
    const auto handle = Open(0x1000, 0, 0, nullptr, nullptr);
    if (handle <= 0) { return 1; }
    std::atomic<int> result{-1};
    std::jthread window([&] { result.store(VideoDemo()); });
    std::uint32_t previous = ~0u; bool previous_focus = false;
    while (result.load() == -1) {
        PadData data;
        const auto status = Read(handle, &data, 1, true);
        if (status != 0) { UnsupportedApi("native_pad_demo_read", static_cast<unsigned>(status), nullptr); }
        bool focused{};
        { auto& state = State(); std::lock_guard lock(state.mutex); focused = state.keyboard.focused; }
        if (data.buttons != previous || focused != previous_focus) {
            std::printf("{\"event\":\"native_pad_demo_input\",\"buttons\":%u,\"focused\":%s,\"timestamp_us\":%llu}\n",
                data.buttons, focused ? "true" : "false", data.timestamp);
            std::fflush(stdout); previous = data.buttons; previous_focus = focused;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    window.join();
    return Close(handle) == 0 ? result.load() : 1;
}
