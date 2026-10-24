#include "input_state.h"
#include "common/stick_deadzone.h"
#include <algorithm>
#include <cmath>

namespace cave::xbox360 {
KeyboardSnapshot KeyboardState::Snapshot(bool active,std::uint16_t source_key) const {
    return BuildSnapshot(active,source_key,false);
}
KeyboardSnapshot KeyboardState::PollFrame(std::uint64_t frame,bool active) {
    if (!active || !focused_) {ClearKeys();return {};}
    if (frame!=frame_) {
        for (std::size_t key=0;key<pending_.size();++key)
            if (latched_[key]) pending_[key]=false;
        latched_.fill(false);
        frame_=frame;
    }
    const auto result=BuildSnapshot(true,0,true);
    sampled_=held_;
    latched_=pending_;
    return result;
}
KeyboardSnapshot KeyboardState::BuildSnapshot(bool active,std::uint16_t source_key,bool pending) const {
    KeyboardSnapshot result;
    if (!focused_ || !active) return result;
    constexpr std::array<std::uint16_t,20> masks{
        1,2,4,8,0x1000,0x2000,0x4000,0x8000,0x10,0x20,0x100,0x200,0,0,0x40,0x80,0,0,0,0};
    std::array<bool,4> right_stick{};
    for (std::size_t n=0; n<bindings_.size(); ++n) {
        const auto& keys = bindings_[n].keys;
        if (std::any_of(keys.begin(),keys.end(),[&](auto key) {
                if (source_key && key!=source_key &&
                    !(key==16 && (source_key==160 || source_key==161)) &&
                    !(key==17 && (source_key==162 || source_key==163)) &&
                    !(key==18 && (source_key==164 || source_key==165))) return false;
                auto pressed=[&](auto code) {return held_[code] || (pending && pending_[code]);};
                if (pressed(key)) return true;
                if (key==16) return pressed(160) || pressed(161);
                if (key==17) return pressed(162) || pressed(163);
                if (key==18) return pressed(164) || pressed(165);
                return false;
            })) {
            result.buttons |= masks[n];
            if (n==12) result.left_trigger=255;
            if (n==13) result.right_trigger=255;
            if (n>=16) right_stick[n-16]=true;
        }
    }
    const int x=int(right_stick[3])-int(right_stick[2]);
    const int y=int(right_stick[0])-int(right_stick[1]);
    const int magnitude=x && y?23170:32767;
    result.thumb_rx=std::int16_t(x*magnitude);
    result.thumb_ry=std::int16_t(y*magnitude);
    return result;
}
std::pair<std::int16_t,std::int16_t> ApplyDeadzone(std::int16_t x, std::int16_t y, double deadzone) {
    return cave::ApplyStickDeadzone(x, y, deadzone);
}
}
