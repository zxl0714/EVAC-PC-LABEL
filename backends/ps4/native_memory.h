#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
namespace kt::memory { class WriteStamp; }

std::uint64_t ResolveMemoryImport(std::string_view identifier);
bool MemorySelfTest();

// Pins keep consumers from outliving their shared backing or losing required
// CPU/GPU permissions. The default is CPU R/W for arenas.
bool RetainNativeMemoryRange(void* address, std::uint64_t size, std::uint64_t* physical_out, unsigned required_protection = 3);
struct NativeMemoryRangeInfo {
    std::uint64_t address{}, bytes{}, physical{}, allocation_identity{};
    std::uint64_t mapping_address{}, mapping_bytes{};
    unsigned logical_protection{}, retained_protection{};
    bool operator==(const NativeMemoryRangeInfo&) const = default;
};
// Same lease/drop contract as RetainNativeMemoryRange. Identity is shared by
// physical aliases and never reused, including after physical-address reuse.
// logical_protection is a snapshot; only retained_protection is held stable.
bool RetainNativeMemoryRangeInfo(void* address, std::uint64_t size, NativeMemoryRangeInfo* output,
                                unsigned required_protection = 3);
void ReleaseNativeMemoryRange(void* address, unsigned required_protection = 3);
// Empty proof means the caller must retain complete-content validation.
kt::memory::WriteStamp TrackNativeMemoryRange(void* address, std::uint64_t size);
// Tests the guest's logical CPU write rights even when optional texture
// tracking temporarily guards a RW mapping. Does not change permissions.
bool NativeMemoryWritable(const void* address, std::uint64_t size);
class NativeHostWrite {
public:
    NativeHostWrite() = default;
    NativeHostWrite(NativeHostWrite&&) noexcept = default;
    NativeHostWrite& operator=(NativeHostWrite&&) noexcept = default;
    NativeHostWrite(const NativeHostWrite&) = delete;
    NativeHostWrite& operator=(const NativeHostWrite&) = delete;
    explicit operator bool() const noexcept { return bool(state_); }
private:
    friend NativeHostWrite BeginNativeHostWrite(void*, std::uint64_t);
    std::shared_ptr<void> state_;
};
// Hold until a host/kernel write has actually completed. Pins every direct
// mapping in the destination, and excludes all physical aliases from Track.
// Non-direct buffers are validated with Windows permissions, not reprotected.
NativeHostWrite BeginNativeHostWrite(void* address, std::uint64_t size);
bool CopyToNativeMemory(void* destination, const void* owned_source, std::uint64_t size);

// GPU cache integration owns temporary CPU guards, never the guest's logical
// permissions. Attach only at a quiescent backend boundary; old content proofs
// are permanently disabled. Callbacks run outside the allocator mutex and may
// dispatch synchronously to a GPU worker that queries/copies/protects memory.
struct NativeGpuMemoryObserver {
    virtual ~NativeGpuMemoryObserver() = default;
    // Invalidate old buffer/texture state in this VA range before registering
    // the new map: it may previously have been a sparse hole or another life.
    virtual void Mapped(const NativeMemoryRangeInfo& mapping) = 0;
    virtual void BeforeChange(std::span<const NativeMemoryRangeInfo> mappings) = 0;
    virtual void BeforeHostWrite(std::span<const NativeMemoryRangeInfo> ranges) = 0;
    virtual void AfterHostWrite(std::span<const NativeMemoryRangeInfo> ranges) = 0;
    virtual void Drain() = 0;
};
bool AttachNativeGpuMemory(std::shared_ptr<NativeGpuMemoryObserver> observer);
// Stop guest access before detach. Waits for host-write leases and drains GPU
// work before dropping guards; cache callbacks must still be alive here.
void DetachNativeGpuMemory(const NativeGpuMemoryObserver* observer);
bool QueryNativeMemoryRange(std::uint64_t address, std::uint64_t bytes, NativeMemoryRangeInfo* output);
std::vector<NativeMemoryRangeInfo> NativeMemoryAliases(std::uint64_t address, std::uint64_t bytes);
// Copies via an unguarded view of the SAME section, not a second allocation.
// Sparse holes read as zero. Mapped reads/writes require GPU R/W respectively.
bool ReadNativeGpuMemory(std::uint64_t address, void* destination, std::uint64_t bytes);
bool WriteNativeGpuMemory(std::uint64_t address, const void* source, std::uint64_t bytes);
bool ProtectNativeGpuMemory(std::uint64_t address, std::uint64_t bytes, unsigned cpu_permissions);
bool NativeGpuMemoryFault(std::uint64_t address, bool write);
