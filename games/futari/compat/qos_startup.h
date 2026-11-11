#pragma once

#include <cstdint>

namespace futari::compat {
// The guest CreateThread wrapper maps Win32 CREATE_SUSPENDED (4) to the
// Xbox ExCreateThread suspended bit (1). Preserve any other creation flags.
constexpr uint32_t SuspendQosCreate(uint32_t flags) { return flags | 4u; }

template <class Read32>
uint32_t PublishedQosThread(uint32_t owner, Read32 read) {
    const auto system = owner ? read(owner + 76) : 0;
    const auto network = system ? read(system + 72) : 0;
    return network ? read(network + 28) : 0;
}
}
