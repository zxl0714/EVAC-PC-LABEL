#pragma once
#include <cstdint>

namespace kt::diagnostics {
// Contiguous host wall-time scopes on the resident GPU worker. Fence waits
// include driver/scheduling time; these are not GPU timestamp queries.
struct BackendPresentTiming {
    bool completed{};
    std::uint64_t duration_us{}, prepare_us{}, retire_us{}, acquire_us{}, record_us{},
        submit_us{}, queue_present_us{}, gpu_wait_us{}, present_wait_us{}, finalize_us{};
};
}
