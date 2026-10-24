#pragma once
#include "common/settings.h"
#include <array>
#include <utility>

namespace cave::xbox360 {
struct KeyboardSnapshot {
    std::uint16_t buttons{};
    std::uint8_t left_trigger{}, right_trigger{};
    std::int16_t thumb_rx{}, thumb_ry{};
    bool operator==(const KeyboardSnapshot&) const = default;
};
class KeyboardState {
public:
    explicit KeyboardState(const Settings& settings) {
        for (std::size_t n=0;n<settings.keyboard.size();++n) bindings_[n]=settings.keyboard[n];
        for (std::size_t n=0;n<settings.keyboard_right_stick.size();++n)
            bindings_[settings.keyboard.size()+n]=settings.keyboard_right_stick[n];
    }
    void SetFocus(bool focused) { focused_=focused; if (!focused) ClearKeys(); }
    void ClearKeys() { held_.fill(false); sampled_.fill(false); pending_.fill(false); latched_.fill(false); }
    void SetKey(std::uint16_t key, bool down) {
        if (key>=held_.size() || (!focused_ && down)) return;
        if (down && !held_[key]) {
            sampled_[key]=false;
            pending_[key]=false;
            latched_[key]=false;
        }
        if (!down && held_[key] && !sampled_[key]) pending_[key]=true;
        held_[key]=down;
    }
    KeyboardSnapshot Snapshot(bool active=true,std::uint16_t source_key=0) const;
    // A complete unsampled tap is visible to all reads in one guest frame.
    // An already-sampled held key still releases immediately, even that frame.
    KeyboardSnapshot PollFrame(std::uint64_t frame,bool active=true);
private:
    KeyboardSnapshot BuildSnapshot(bool active,std::uint16_t source_key,bool pending) const;
    std::array<Binding,20> bindings_;
    std::array<bool,256> held_{};
    std::array<bool,256> sampled_{};
    std::array<bool,256> pending_{};
    std::array<bool,256> latched_{};
    std::uint64_t frame_{};
    bool focused_{};
};
std::pair<std::int16_t,std::int16_t> ApplyDeadzone(std::int16_t x, std::int16_t y, double deadzone);
}
