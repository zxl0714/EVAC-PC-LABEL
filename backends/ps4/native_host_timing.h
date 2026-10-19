#pragma once
#include <chrono>
#include <cstdint>

enum class NativeHostAllocationKind : unsigned {
    Unclassified, New, NothrowNew, Malloc, Memalign, FreeDelete,
    MspaceCreate, MspaceDestroy, MspaceMemalign, MspaceFree
};

// Host wall time only. Scopes never change guest deadlines, locks or results.
// Names/categories are static runtime labels, not strings read from the guest.
class NativeHostOperationScope {
public:
    NativeHostOperationScope(const char* name, const char* category, std::uint64_t detail,
                             const void* caller, bool enabled = true,
                             NativeHostAllocationKind allocation = NativeHostAllocationKind::Unclassified) noexcept;
    ~NativeHostOperationScope() noexcept;
    NativeHostOperationScope(const NativeHostOperationScope&) = delete;
    NativeHostOperationScope& operator=(const NativeHostOperationScope&) = delete;
private:
    const char* name_;
    const char* category_;
    std::uint64_t detail_;
    const void* caller_;
    bool enabled_;
    bool outer_{};
    bool counts_only_{};
    unsigned category_index_{};
    std::uint64_t began_submission_{};
    NativeHostAllocationKind allocation_;
    std::chrono::steady_clock::time_point began_;
};

void PrintNativeHostTimingDiagnostics() noexcept;
// Startup-only diagnostic choice. Reject changes once any enabled scope starts.
bool ConfigureNativeAllocationTiming(bool full) noexcept;
// Reset only the current thread's counters on EVERY completed frame; emit a
// bounded-size summary only for a completion interval at least 25 ms.
void RecordNativeHostFrameTiming(std::uint64_t submission, std::uint64_t clock_us,
                                std::uint64_t interval_us) noexcept;
bool NativeHostTimingSelfTest();
bool NativeHostCountsOnlySelfTest(bool configure = true);

// Defined beside the image registry, keeping runtime-relative caller lookup out
// of the timer and executing only after the operation's host locks are gone.
bool EmitNativeHostOperation(const char* name, const char* category, std::uint64_t detail,
                            const void* caller, std::uint64_t began, std::uint64_t completed,
                            std::uint64_t suppressed) noexcept;
