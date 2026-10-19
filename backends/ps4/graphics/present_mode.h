#pragma once
#include <algorithm>
#include <span>

namespace cave::ps4 {
// FIFO is required by Vulkan. Disabling host vsync does not alter guest clocks.
template<typename Mode>
Mode SelectPresentMode(bool vsync, std::span<const Mode> supported, Mode fifo, Mode immediate, Mode mailbox) {
    if (!vsync) {
        if (std::find(supported.begin(), supported.end(), immediate) != supported.end()) return immediate;
        if (std::find(supported.begin(), supported.end(), mailbox) != supported.end()) return mailbox;
    }
    return fifo;
}
}
