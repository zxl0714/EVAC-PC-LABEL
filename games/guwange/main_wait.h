#pragma once

#include <cstdint>

namespace cave::guwange {

// The guest loads this register once, before its cmp/beq self-loop. A zero
// sample never changes in that loop; rereading guest memory would invent a
// different exit condition. Keep it alive until normal title termination.
template <class CheckTermination, class Wait>
void KeepMainThreadAlive(std::uint32_t sampled_flag, CheckTermination&& check, Wait&& wait) {
    if (sampled_flag != 0) return;
    for (;;) {
        check();
        wait();
    }
}

}  // namespace cave::guwange
