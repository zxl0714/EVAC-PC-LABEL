#pragma once
#include <cstdint>

namespace rex::input {
// One epoch per actual guest VBlank, independent of host presentation rate.
// A process runs one guest title; no window or CPU-frequency clock is involved.
uint64_t GuestInputFrame();
void AdvanceGuestInputFrame();
}
