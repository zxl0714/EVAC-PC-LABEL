#pragma once
#include <cstdint>

namespace cave::ps2 {
// Status is sticky/coalesced until EE observes it. Signal and label halves of
// SIGLBLID are independent; masked label updates can be combined in order.
struct GsCompletions {
    uint64_t generation=0,signalPayload=0,labelPayload=0;
    bool signal=false,finish=false,label=false;
    explicit operator bool() const{return signal || finish || label;}
};
}
