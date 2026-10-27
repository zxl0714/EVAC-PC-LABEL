#pragma once
#include "../../../backends/xbox360/performance/signal_wait.h"
#include <cstdint>

namespace sdoj::performance {
inline bool TickPending(std::uint32_t current, std::uint32_t baseline, std::int32_t depth) {
    return static_cast<std::int32_t>(current - baseline) < depth;
}
// One retained sequence covers both the original frame counter and teardown
// request. Read it BEFORE checking the guest predicates, so neither producer
// can notify in the gap and leave an unchanged counter asleep.
using TickWait = cave::xbox360::performance::SignalWait;
}
