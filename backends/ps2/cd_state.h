#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace ps2_stubs {
    std::vector<uint8_t> saveCdState();
    void loadCdState(std::span<const uint8_t> state);
}
