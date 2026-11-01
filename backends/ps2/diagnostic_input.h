#pragma once
#include <cstdint>

namespace cave::ps2 {
// Configure before running the host loop. Zero preserves ordinary input.
void ConfigureDiagnosticInput(uint32_t tapMilliseconds);
// The window-owning thread drains Raylib's key event queue.
void PollDiagnosticInput(uint64_t nowMilliseconds);
}
