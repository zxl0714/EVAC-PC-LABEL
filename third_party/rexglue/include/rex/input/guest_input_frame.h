#pragma once
#include <cstdint>

namespace rex::input {
// One epoch per actual guest VBlank, independent of host presentation rate.
// A process runs one guest title; no window or CPU-frequency clock is involved.
uint64_t GuestInputFrame();
void AdvanceGuestInputFrame();
// Read-only consumer view of the existing video worker's next deadline, in
// steady_clock microseconds. Zero means startup/catch-up/shutdown: no budget.
uint64_t GuestVblankDeadlineMicros();
// Video worker only. Publishing this does not advance or reschedule a VBlank.
void PublishGuestVblankDeadlineMicros(uint64_t deadline);
}
