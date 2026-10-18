#include "configured_input.h"
#include "input_state.h"
#include <array>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/logging.h>
#include <rex/cvar.h>
#include <rex/ui/virtual_key.h>

REXCVAR_DEFINE_BOOL(input_diagnostics,false,"Input","Bounded keyboard event and guest poll diagnostics");

namespace espgaluda2 {
namespace {
using namespace rex;
using namespace rex::input;

class KeyboardDriver final : public InputDriver, public ui::WindowInputListener, public ui::WindowListener {
public:
    explicit KeyboardDriver(const Settings& settings) : InputDriver(nullptr,127), state_(settings), trace_(REXCVAR_GET(input_diagnostics)) {}
    ~KeyboardDriver() override { Detach(); }
    X_STATUS Setup() override { return X_STATUS_SUCCESS; }
    bool IsKeyboardEmulation() const override { return true; }
    void OnWindowAvailable(ui::Window* target) override {
        Detach();
        target_=target;
        if (target_) {
            // Receive releases before overlays consume them. Downs are gated
            // by is_active(), so typing in a dialog never controls the game.
            target_->AddInputListener(this,127);
            target_->AddListener(this);
            std::lock_guard lock(mutex_);
            state_.SetFocus(target_->HasFocus());
            if (trace_) REXLOG_INFO("INPUT_DIAG attached focus={}",target_->HasFocus());
        }
    }
    X_RESULT GetCapabilities(uint32_t user, uint32_t, X_INPUT_CAPABILITIES* caps) override {
        if (user) return X_ERROR_DEVICE_NOT_CONNECTED;
        if (!caps) return X_ERROR_BAD_ARGUMENTS;
        *caps={}; caps->type=1; caps->sub_type=1;
        caps->gamepad.buttons=0xF3FF;
        caps->gamepad.left_trigger=caps->gamepad.right_trigger=255;
        return X_ERROR_SUCCESS;
    }
    X_RESULT GetState(uint32_t user, X_INPUT_STATE* output) override {
        if (user) return X_ERROR_DEVICE_NOT_CONNECTED;
        if (!output) return X_ERROR_BAD_ARGUMENTS;
        const auto active=is_active();
        std::lock_guard lock(mutex_);
        if (!active) {state_.ClearKeys();events_.clear();}
        const auto snapshot=state_.Poll(active);
        ++polls_;
        if (trace_ && trace_remaining_ && (snapshot!=last_ || (polls_ & (polls_-1))==0)) {
            --trace_remaining_;
            REXLOG_INFO("INPUT_DIAG poll count={} active={} buttons={:04X}",polls_,active,snapshot.buttons);
        }
        if (snapshot!=last_) {last_=snapshot; ++packet_;}
        *output={}; output->packet_number=packet_; output->gamepad.buttons=snapshot.buttons;
        output->gamepad.left_trigger=snapshot.left_trigger;
        output->gamepad.right_trigger=snapshot.right_trigger;
        return X_ERROR_SUCCESS;
    }
    X_RESULT SetState(uint32_t user,X_INPUT_VIBRATION*) override {
        return user ? X_ERROR_DEVICE_NOT_CONNECTED : X_ERROR_SUCCESS;
    }
    X_RESULT GetKeystroke(uint32_t user,uint32_t,X_INPUT_KEYSTROKE* output) override {
        if (user && user!=0xFF) return X_ERROR_DEVICE_NOT_CONNECTED;
        if (!output) return X_ERROR_BAD_ARGUMENTS;
        const auto active=is_active();
        std::lock_guard lock(mutex_);
        if (!active) {state_.ClearKeys();events_.clear();}
        if (events_.empty()) return X_ERROR_EMPTY;
        *output=events_.front(); events_.pop_front();
        return X_ERROR_SUCCESS;
    }
    void OnKeyDown(ui::KeyEvent& event) override {
        const bool active=is_active();
        if (!event.is_handled() && active) Key(static_cast<uint16_t>(event.virtual_key()),true,event.prev_state());
        else if (trace_) {
            std::lock_guard lock(mutex_);
            if (trace_remaining_) {--trace_remaining_; REXLOG_INFO("INPUT_DIAG ignored key={} active={} handled={}",static_cast<uint16_t>(event.virtual_key()),active,event.is_handled());}
        }
    }
    void OnKeyUp(ui::KeyEvent& event) override { Key(static_cast<uint16_t>(event.virtual_key()),false); }
    void OnGotFocus(ui::UISetupEvent&) override { std::lock_guard lock(mutex_); state_.SetFocus(true); }
    void OnLostFocus(ui::UISetupEvent&) override { std::lock_guard lock(mutex_); state_.SetFocus(false); events_.clear(); }
    void OnClosing(ui::UIEvent&) override { Detach(); }
private:
    void Detach() {
        if (!target_) return;
        target_->RemoveInputListener(this); target_->RemoveListener(this); target_=nullptr;
        std::lock_guard lock(mutex_); state_.SetFocus(false); events_.clear();
    }
    void Key(uint16_t key,bool down,bool repeat=false) {
        std::lock_guard lock(mutex_);
        const auto before=state_.Snapshot();
        state_.SetKey(key,down);
        const auto after=state_.Snapshot();
        if (trace_ && trace_remaining_) {
            --trace_remaining_;
            REXLOG_INFO("INPUT_DIAG key={} down={} repeat={} before={:04X} after={:04X} polls={}",key,down,repeat,before.buttons,after.buttons,polls_);
        }
        const auto oldBits=uint32_t(before.buttons)|(before.left_trigger?0x10000:0)|(before.right_trigger?0x20000:0);
        const auto newBits=uint32_t(after.buttons)|(after.left_trigger?0x10000:0)|(after.right_trigger?0x20000:0);
        const auto source=state_.Snapshot(true,key);
        const auto repeats=repeat ? uint32_t(source.buttons)|(source.left_trigger?0x10000:0)|(source.right_trigger?0x20000:0) : 0;
        constexpr std::array<std::pair<uint32_t,ui::VirtualKey>,16> keys{{
            {1,ui::VirtualKey::kXInputPadDpadUp},{2,ui::VirtualKey::kXInputPadDpadDown},
            {4,ui::VirtualKey::kXInputPadDpadLeft},{8,ui::VirtualKey::kXInputPadDpadRight},
            {0x10,ui::VirtualKey::kXInputPadStart},{0x20,ui::VirtualKey::kXInputPadBack},
            {0x40,ui::VirtualKey::kXInputPadLThumbPress},{0x80,ui::VirtualKey::kXInputPadRThumbPress},
            {0x100,ui::VirtualKey::kXInputPadLShoulder},{0x200,ui::VirtualKey::kXInputPadRShoulder},
            {0x1000,ui::VirtualKey::kXInputPadA},{0x2000,ui::VirtualKey::kXInputPadB},
            {0x4000,ui::VirtualKey::kXInputPadX},{0x8000,ui::VirtualKey::kXInputPadY},
            {0x10000,ui::VirtualKey::kXInputPadLTrigger},{0x20000,ui::VirtualKey::kXInputPadRTrigger}}};
        for (const auto& [bit,padKey] : keys) if (((oldBits^newBits)|repeats)&bit) {
            X_INPUT_KEYSTROKE stroke{};
            stroke.virtual_key=static_cast<uint16_t>(padKey);
            stroke.flags=(newBits&bit)?X_INPUT_KEYSTROKE_KEYDOWN:X_INPUT_KEYSTROKE_KEYUP;
            if (!((oldBits^newBits)&bit) && (repeats&bit)) stroke.flags=X_INPUT_KEYSTROKE_KEYDOWN|X_INPUT_KEYSTROKE_REPEAT;
            if (events_.size()==64) events_.pop_front();
            events_.push_back(stroke);
        }
    }
    ui::Window* target_{};
    std::mutex mutex_;
    KeyboardState state_;
    KeyboardSnapshot last_{};
    uint32_t packet_{};
    std::deque<X_INPUT_KEYSTROKE> events_;
    bool trace_{};
    uint32_t trace_remaining_{256};
    uint64_t polls_{};
};

class GamepadDriver final : public InputDriver {
public:
    explicit GamepadDriver(double deadzone) : InputDriver(nullptr,10), inner_(nullptr,10), deadzone_(deadzone) {}
    X_STATUS Setup() override { return inner_.Setup(); }
    void OnWindowAvailable(ui::Window* window) override {
        inner_.set_is_active_callback([this]{return is_active();});
        inner_.OnWindowAvailable(window);
    }
    X_RESULT GetCapabilities(uint32_t user,uint32_t flags,X_INPUT_CAPABILITIES* caps) override {
        return inner_.GetCapabilities(user,flags,caps);
    }
    X_RESULT GetState(uint32_t user,X_INPUT_STATE* state) override {
        auto result=inner_.GetState(user,state);
        if (result==X_ERROR_SUCCESS && state) {
            auto [lx,ly]=ApplyDeadzone(state->gamepad.thumb_lx,state->gamepad.thumb_ly,deadzone_);
            auto [rx,ry]=ApplyDeadzone(state->gamepad.thumb_rx,state->gamepad.thumb_ry,deadzone_);
            state->gamepad.thumb_lx=lx; state->gamepad.thumb_ly=ly;
            state->gamepad.thumb_rx=rx; state->gamepad.thumb_ry=ry;
        }
        return result;
    }
    X_RESULT SetState(uint32_t user,X_INPUT_VIBRATION* vibration) override {return inner_.SetState(user,vibration);}
    X_RESULT GetKeystroke(uint32_t user,uint32_t flags,X_INPUT_KEYSTROKE* stroke) override {
        return inner_.GetKeystroke(user,flags,stroke);
    }
private:
    sdl::SDLInputDriver inner_;
    double deadzone_;
};
}
std::unique_ptr<rex::system::IInputSystem> CreateConfiguredInput(const Settings& settings,bool tool_mode) {
    if (tool_mode) throw std::runtime_error("Configured player input must not initialize in tool mode");
    auto input=std::make_unique<rex::input::InputSystem>(nullptr);
    if (settings.gamepad_enabled) {
        auto pad=std::make_unique<GamepadDriver>(settings.deadzone);
        if (pad->Setup()!=X_STATUS_SUCCESS) throw std::runtime_error("SDL gamepad setup failed");
        input->AddDriver(std::move(pad));
    }
    auto keyboard=std::make_unique<KeyboardDriver>(settings);
    keyboard->Setup(); input->AddDriver(std::move(keyboard));
    REXLOG_INFO("Input: keyboard on slot 0; SDL gamepad={} deadzone={} mouse_capture=false",settings.gamepad_enabled,settings.deadzone);
    return input;
}
}
