#include "input_state.h"
#include <algorithm>
#include <cmath>

namespace cave::xbox360 {
KeyboardSnapshot KeyboardState::Snapshot(bool active,std::uint16_t source_key) const {
    return BuildSnapshot(active,source_key,false);
}
KeyboardSnapshot KeyboardState::Poll(bool active) {
    if (!active || !focused_) {ClearKeys();return {};}
    const auto result=BuildSnapshot(true,0,true);
    sampled_=held_;
    pending_.fill(false);
    return result;
}
KeyboardSnapshot KeyboardState::BuildSnapshot(bool active,std::uint16_t source_key,bool pending) const {
    KeyboardSnapshot result;
    if (!focused_ || !active) return result;
    constexpr std::array<std::uint16_t,16> masks{
        1,2,4,8,0x1000,0x2000,0x4000,0x8000,0x10,0x20,0x100,0x200,0,0,0x40,0x80};
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
        }
    }
    return result;
}
std::pair<std::int16_t,std::int16_t> ApplyDeadzone(std::int16_t x, std::int16_t y, double deadzone) {
    const double nx = x/(x<0 ? 32768.0 : 32767.0), ny = y/(y<0 ? 32768.0 : 32767.0);
    const auto magnitude = std::hypot(nx,ny);
    if (magnitude<=deadzone || !magnitude) return {};
    const auto scale = (std::min(magnitude,1.0)-deadzone)/((1.0-deadzone)*magnitude);
    auto convert = [](double value) {
        return static_cast<std::int16_t>(std::lround(std::clamp(value,-1.0,1.0)*(value<0 ? 32768.0 : 32767.0)));
    };
    return {convert(nx*scale),convert(ny*scale)};
}
}
