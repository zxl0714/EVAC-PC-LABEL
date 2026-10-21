#include "test_pad.h"
#include <rex/runtime.h>
#include <rex/input/input_system.h>
#include <rex/input/input_driver.h>
#include <rex/input/guest_input_frame.h>
#include <rex/logging.h>
#include <fstream>
#include <sstream>
#include <mutex>
#include <stdexcept>

namespace sdoj::diagnostics {
namespace {
using rex::X_STATUS;
using rex::X_RESULT;
class TestPad final : public rex::input::InputDriver {
public:
    TestPad(std::filesystem::path path, std::function<void()> quit)
        : InputDriver(nullptr, 0), path_(std::move(path)), quit_(std::move(quit)) {}
    rex::X_STATUS Setup() override { return X_STATUS_SUCCESS; }
    // The file specifies logical Xbox buttons, after the physical remap stage.
    bool IsKeyboardEmulation() const override { return true; }
    rex::X_RESULT GetCapabilities(uint32_t user, uint32_t, rex::input::X_INPUT_CAPABILITIES* out) override {
        if (user) return X_ERROR_DEVICE_NOT_CONNECTED;
        if (!out) return X_ERROR_BAD_ARGUMENTS;
        *out={}; out->type=1; out->sub_type=1; out->gamepad.buttons=0xF3FF;
        return X_ERROR_SUCCESS;
    }
    rex::X_RESULT GetState(uint32_t user, rex::input::X_INPUT_STATE* out) override {
        if (user) return X_ERROR_DEVICE_NOT_CONNECTED;
        if (!out) return X_ERROR_BAD_ARGUMENTS;
        std::lock_guard lock(gate_);
        const auto frame=rex::input::GuestInputFrame();
        if (frame>=next_read_) { next_read_=frame+6; Read(frame); }
        const uint16_t value=frame<release_ ? buttons_ : 0;
        if(value!=previous_) {
            previous_=value; ++packet_;
            REXLOG_INFO("SDOJ_TEST_PAD phase=state ticket={} frame={} buttons={:04X}",ticket_,frame,value);
        }
        *out={}; out->packet_number=packet_; out->gamepad.buttons=value;
        return X_ERROR_SUCCESS;
    }
    rex::X_RESULT SetState(uint32_t user, rex::input::X_INPUT_VIBRATION*) override {
        return user ? X_ERROR_DEVICE_NOT_CONNECTED : X_ERROR_SUCCESS;
    }
    rex::X_RESULT GetKeystroke(uint32_t user, uint32_t, rex::input::X_INPUT_KEYSTROKE*) override {
        return user && user!=0xff ? X_ERROR_DEVICE_NOT_CONNECTED : X_ERROR_EMPTY;
    }
private:
    void Read(uint64_t frame) {
        std::ifstream file(path_);
        std::string line;
        if(!std::getline(file,line) || line.size()>96) return;
        std::istringstream fields(line);
        uint64_t ticket{};
        std::string action, extra;
        if(!(fields>>ticket>>action) || ticket<=ticket_) return;
        if(action=="quit") {
            if(fields>>extra) return;
            ticket_=ticket; buttons_=0; release_=0;
            REXLOG_INFO("SDOJ_TEST_PAD phase=quit ticket={} frame={}",ticket,frame);
            quit_(); return;
        }
        uint32_t buttons{},frames{};
        if(action!="press" || !(fields>>std::hex>>buttons>>std::dec>>frames) ||
           (fields>>extra) || (buttons&~0xF3FFu) || !frames || frames>600) return;
        // Do not truncate an acknowledged pulse if a later command arrives early.
        if(frame<release_) return;
        ticket_=ticket; buttons_=uint16_t(buttons); release_=frame+frames;
        REXLOG_INFO("SDOJ_TEST_PAD phase=ack ticket={} frame={} buttons={:04X} duration={}",
                    ticket,frame,buttons,frames);
    }
    std::filesystem::path path_;
    std::function<void()> quit_;
    std::mutex gate_;
    uint64_t ticket_{},release_{},next_read_{};
    uint32_t packet_{};
    uint16_t buttons_{},previous_{};
};
}
void AddTestPad(rex::RuntimeConfig& config,const std::filesystem::path& path,std::function<void()> quit) {
    if(!path.is_absolute() || !std::filesystem::is_regular_file(path))
        throw std::runtime_error("SDOJ test pad requires an existing absolute command file");
    auto factory=std::move(config.input_factory);
    config.input_factory=[factory=std::move(factory),path,quit=std::move(quit)](bool tool_mode) {
        auto result=factory(tool_mode);
        auto* input=dynamic_cast<rex::input::InputSystem*>(result.get());
        if(!input || tool_mode) throw std::runtime_error("SDOJ test pad requires configured gameplay input");
        input->AddDriver(std::make_unique<TestPad>(path,quit));
        REXLOG_WARN("SDOJ_TEST_PAD enabled=true source=diagnostic_file physical_input_test=false");
        return result;
    };
}
}
