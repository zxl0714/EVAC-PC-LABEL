#pragma once
#include <Windows.h>
#include <chrono>

namespace cave::ps2 {
// Private benchmark candidate, not adopted into the player runtime.
// Queue status is a hint, so retain periodic full sweeps for internal events.
// Callers keep their existing input-poll deadline and drain without filtering.
class WindowMessagePoll {
    std::chrono::steady_clock::time_point nextSweep{};
public:
    bool Due(std::chrono::steady_clock::time_point now) {
        if(now>=nextSweep) {
            nextSweep=now+std::chrono::milliseconds(8);
            return true;
        }
        return HIWORD(GetQueueStatus(QS_ALLINPUT))!=0;
    }
};
}
