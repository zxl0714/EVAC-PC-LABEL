#pragma once
#include <array>
#include <cstdint>

namespace cave {
// Swapping the two cabinet seats is its own inverse.
constexpr unsigned ControllerPort(unsigned port, bool swap_players) {
    return swap_players && port < 2 ? 1u-port : port;
}
// Prefer the OS player index. Keep all existing assignments on hotplug.
inline int AvailableControllerPort(const std::array<std::uint32_t,2>& ids, int preferred) {
    if (preferred>=0 && preferred<2 && !ids[preferred])return preferred;
    if (!ids[0])return 0;
    if (!ids[1])return 1;
    return -1;
}
}
