#pragma once
#include <cstdint>

namespace kt::diagnostics {
// Host work scopes on the resident GPU worker. duration_us is their sum and
// excludes the separately reported gap between prepare and complete. That gap
// includes caller pacing/worker handoff and can overlap submitted GPU work.
// Fence waits include driver/scheduling time; these are not GPU timestamps.
struct BackendPresentTiming {
    bool completed{};
    std::uint64_t duration_us{}, prepare_us{}, retire_us{}, acquire_us{}, record_us{},
        submit_us{}, queue_present_us{}, gpu_wait_us{}, present_wait_us{}, finalize_us{};
    std::uint64_t between_phases_us{};
};
// Host callback on the calling thread, never guest code or a GPU worker wait.
// Called between prepare/complete while the worker remains free for coherent
// memory requests. false cancels guest completion, not GPU resource retirement.
struct PresentationPacing { void* context{}; bool (*wait)(void*){}; };
}
