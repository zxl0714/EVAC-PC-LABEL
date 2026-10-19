#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace kt::memory {
struct WatchedView;
struct WatchedRange;
struct WatchedHostWrite;
// A generation is usable only while all writable aliases remain armed. It is
// not a content hash. Capture it before copying; check again after copying.
class WriteStamp {
public:
    bool Unchanged() const noexcept;
    explicit operator bool() const noexcept { return bool(range_); }
private:
    friend class WriteTracker;
    std::shared_ptr<WatchedRange> range_;
    std::uint64_t generation_{};
};
struct WriteTrackerStats { std::uint64_t faults{}, arms{}, protected_bytes{}; bool enabled{}; };

// Windows mapped-section write tracking. Callers register EVERY alias and use
// a never-reused allocation identity. Register/remove/protect operations must
// bracket their mapping lifecycle; no access may race removal/unmapping.
// Only explicitly registered, committed MEM_MAPPED views are protected.
// No game input, clock, GPU state, or memory contents are modified.
class WriteTracker {
    struct Impl;
public:
    using View = std::shared_ptr<WatchedView>;
    explicit WriteTracker(std::size_t maximum_views = 4096, std::size_t maximum_ranges = 256);
    ~WriteTracker();
    WriteTracker(const WriteTracker&) = delete;
    WriteTracker& operator=(const WriteTracker&) = delete;
    View AddView(void* address, std::uint64_t bytes, std::uint64_t physical,
                 std::uint64_t allocation_identity, unsigned windows_protection);
    void RemoveView(const View& view);
    bool ProtectView(const View& view, unsigned windows_protection);
    // Validates logical RW permissions, not the temporary tracking protection.
    // Ordinary, unregistered host memory retains Windows permission checks.
    bool Writable(const void* address, std::uint64_t bytes) const;
    class HostWrite {
    public:
        HostWrite() = default;
        ~HostWrite();
        HostWrite(HostWrite&&) noexcept;
        HostWrite& operator=(HostWrite&&) noexcept;
        HostWrite(const HostWrite&) = delete;
        HostWrite& operator=(const HostWrite&) = delete;
        explicit operator bool() const noexcept { return bool(owner_); }
    private:
        friend class WriteTracker;
        void Reset() noexcept;
        std::shared_ptr<Impl> owner_;
        std::shared_ptr<WatchedHostWrite> write_;
    };
    // Caller must pin the mapping lifetime/permissions through completion.
    // Begin invalidates/restores the destination pages, Track refuses physical
    // overlaps while held, and destruction invalidates again. No bytes change.
    HostWrite BeginHostWrite(void* address, std::uint64_t bytes);
    WriteStamp Track(std::uint64_t allocation_identity, std::uint64_t physical, std::uint64_t bytes);
    void Disable();
    WriteTrackerStats Stats() const noexcept;
    // Also called before the probe's fail-fast handler. No locks, allocations,
    // logging, shared_ptr operations, or game callbacks occur on this path.
    static bool HandleWriteFault(void* address) noexcept;
private:
    std::shared_ptr<Impl> impl_;
};
bool WriteTrackerSelfTest();
}
