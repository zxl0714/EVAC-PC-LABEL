#pragma once
#include <cstdint>

namespace kt::diagnostics {
// Host work scopes on the resident GPU worker. duration_us is their sum and
// excludes the separately reported gap between prepare and complete. That gap
// includes caller pacing/worker handoff and can overlap submitted GPU work.
// Fence waits include driver/scheduling time; these are not GPU timestamps.
// The asynchronous snapshot path reports guest-side work only: gpu_wait_us waits
// for the guest-to-host copy; acquire/queue_present/present_wait are zero here.
// Window-side waits are in native_gpu_window_present, not zero display latency.
struct BackendPresentTiming {
    bool completed{};
    std::uint64_t duration_us{}, prepare_us{}, retire_us{}, acquire_us{}, record_us{},
        submit_us{}, queue_present_us{}, gpu_wait_us{}, present_wait_us{}, finalize_us{};
    std::uint64_t between_phases_us{};
};
// Host callback on the calling thread, never guest code or a GPU worker wait.
// Called after the owned snapshot is published, while the GPU worker remains
// free for coherent memory requests. false cancels guest completion, not GPU
// resource retirement; the independent presentation worker owns published data.
struct PresentationPacing { void* context{}; bool (*wait)(void*){}; };
}
