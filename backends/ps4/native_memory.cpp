#include "native_memory.h"
#include "native_diagnostics.h"
#include "native_profile.h"
#include "memory/write_tracker.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
constexpr std::uint64_t kPage = 0x4000;
constexpr std::uint64_t kGranularity = 0x10000;
constexpr std::uint64_t kHostPage = 0x1000;
// An explicit development pool, not a claim about every PS4 model/SDK. Section
// storage is reserved per allocation and committed only for mapped ranges.
constexpr std::uint64_t kCapacity = kNativeProfile.direct_bytes;
constexpr std::uintptr_t kLowest = 0x1000000000ull; // 64 GiB, separate from the ELF image
constexpr std::uintptr_t kLimit = 0x10000000000ull; // 1 TiB, exclusive
constexpr int kInvalid = static_cast<int>(0x80020016u);
constexpr int kAgain = static_cast<int>(0x80020023u);
constexpr int kNoMemory = static_cast<int>(0x8002000cu);
constexpr int kAccess = static_cast<int>(0x8002000du);
constexpr int kExists = static_cast<int>(0x80020011u);
constexpr int kNotFound = static_cast<int>(0x80020002u);
constexpr int kUnsupported = static_cast<int>(0x8002002du);
constexpr int kNameTooLong = static_cast<int>(0x8002003fu);
constexpr unsigned kFixed = 0x10, kNoOverwrite = 0x80, kNoCoalesce = 0x400000;

std::uint64_t RoundUp(std::uint64_t value, std::uint64_t alignment) {
    const auto remainder = value % alignment;
    return remainder ? value + alignment - remainder : value;
}
bool PageRange(std::uint64_t start, std::uint64_t size) {
    return size && !(start % kPage) && !(size % kPage);
}
bool MapAlignment(std::uint64_t alignment) {
    return !alignment || (std::has_single_bit(alignment) && alignment >= kPage && alignment <= 0x80000000ull);
}
DWORD CpuProtection(int protection) {
    // On x86, writable pages are also readable. GPU flags stay in the mapping
    // record; they are not Windows PAGE_* flags or GPU cache-coherency support.
    return (protection & 2) ? PAGE_READWRITE : ((protection & 1) ? PAGE_READONLY : PAGE_NOACCESS);
}

kt::memory::WriteTracker& WriteTracker() { static kt::memory::WriteTracker tracker; return tracker; }
std::atomic<std::uint64_t> next_allocation_identity{1};
struct Allocation {
    std::uint64_t size{};
    std::uint64_t identity{};
    int type{};
    HANDLE section{};
    void* backing{};
    struct Guard { unsigned read{}, write{}, host_writes{}; };
    std::map<std::uint64_t, Guard> guards;
    Allocation() = default;
    Allocation(const Allocation&) = delete;
    Allocation& operator=(const Allocation&) = delete;
    ~Allocation() { if (backing) { UnmapViewOfFile(backing); } if (section) { CloseHandle(section); } }
};
struct Mapping {
    std::uint64_t size, physical, allocation;
    int protection;
    std::uint64_t pins{};
    std::array<std::uint64_t, 6> required{};
    kt::memory::WriteTracker::View watched{};
    std::map<std::uint64_t, unsigned> gpu_permissions{};
};

class DirectMemory {
public:
    DirectMemory() {
        (void)WriteTracker(); // Construct first; it must outlive global memory.
        const auto kernel = GetModuleHandleW(L"kernelbase.dll");
        allocate_ = reinterpret_cast<AllocateFunction>(GetProcAddress(kernel, "VirtualAlloc2"));
        map_ = reinterpret_cast<MapFunction>(GetProcAddress(kernel, "MapViewOfFile3"));
        unmap_ = reinterpret_cast<UnmapFunction>(GetProcAddress(kernel, "UnmapViewOfFile2"));
        if (!allocate_ || !map_ || !unmap_) { throw std::runtime_error("Windows placeholder mapping APIs unavailable"); }
    }
    ~DirectMemory() {
        for (const auto& [address, mapping] : maps_) { WriteTracker().RemoveView(mapping.watched); UnmapViewOfFile(reinterpret_cast<void*>(address)); }
        for (const auto& [address, bytes] : free_placeholders_) { (void)bytes; VirtualFree(reinterpret_cast<void*>(address), 0, MEM_RELEASE); }
    }

    int Allocate(std::int64_t start, std::int64_t end, std::uint64_t size,
                 std::uint64_t alignment, int type, std::int64_t* output) {
        if (start < 0 || end < 0 || !PageRange(0, size) || alignment % kPage || !output || type < 0 || type > 10) { return kInvalid; }
        if (type != 0 && type != 3 && type != 10) { return kUnsupported; }
        if (end <= start || static_cast<std::uint64_t>(end - start) < size || size > kCapacity) { return kAgain; }
        alignment = alignment ? alignment : kPage;
        const auto limit = (std::min)(static_cast<std::uint64_t>(end), kCapacity);
        if (static_cast<std::uint64_t>(start) >= limit) { return kAgain; }
        auto candidate = RoundUp(static_cast<std::uint64_t>(start), alignment);
        std::lock_guard lifecycle(lifecycle_);
        std::lock_guard lock(mutex_);
        for (const auto& [physical, allocation] : allocations_) {
            if (physical >= candidate && size <= physical - candidate) { break; }
            if (candidate < physical + allocation.size) { candidate = RoundUp(physical + allocation.size, alignment); }
        }
        if (candidate > limit || size > limit - candidate) {
            std::uint64_t allocated{}, largest{}, cursor = RoundUp(static_cast<std::uint64_t>(start), alignment);
            for (const auto& [physical, allocation] : allocations_) {
                allocated += allocation.size;
                if (physical + allocation.size <= cursor || physical >= limit) { continue; }
                if (physical > cursor) { largest = (std::max)(largest, physical - cursor); }
                cursor = RoundUp((std::max)(cursor, physical + allocation.size), alignment);
            }
            if (cursor < limit) { largest = (std::max)(largest, limit - cursor); }
            std::printf("{\"event\":\"direct_memory_capacity_failure\",\"requested_bytes\":%llu,\"capacity_bytes\":%llu,\"allocated_bytes\":%llu,\"largest_aligned_free_bytes\":%llu,\"range_start\":%lld,\"range_end\":%lld,\"alignment\":%llu}\n",
                size, kCapacity, allocated, largest, start, end, alignment);
            return kAgain;
        }
        auto [iterator, inserted] = allocations_.try_emplace(candidate);
        if (!inserted) { return kAgain; }
        auto& allocation = iterator->second;
        const auto backing_size = RoundUp(size, kGranularity);
        allocation.section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_RESERVE,
            static_cast<DWORD>(backing_size >> 32), static_cast<DWORD>(backing_size), nullptr);
        if (!allocation.section) { HostError("create_section"); allocations_.erase(iterator); return kNoMemory; }
        allocation.size = size;
        allocation.identity = next_allocation_identity.fetch_add(1);
        if (!allocation.identity) { throw std::runtime_error("Direct-memory allocation identity exhausted"); }
        allocation.type = type;
        *output = static_cast<std::int64_t>(candidate);
        std::printf("{\"event\":\"direct_memory_allocate\",\"physical\":\"0x%llx\",\"size\":%llu,\"alignment\":%llu,\"type\":%d}\n", candidate, size, alignment, type);
        return 0;
    }

    int Map(void** address, std::uint64_t size, int protection, int flags,
            std::int64_t physical, std::uint64_t alignment) {
        if (!address || physical < 0 || !PageRange(static_cast<std::uint64_t>(physical), size) || !MapAlignment(alignment) ||
            (static_cast<unsigned>(protection) & ~0x37u)) { return kInvalid; }
        if (protection & 4) { return kAccess; }
        if (static_cast<unsigned>(flags) & ~(kFixed | kNoOverwrite | kNoCoalesce)) { return kUnsupported; }
        const auto requested = reinterpret_cast<std::uintptr_t>(*address);
        if ((flags & kFixed) && (!requested || requested % kPage)) { return kInvalid; }
        alignment = alignment ? alignment : kPage;
        if ((flags & kFixed) && requested % alignment) { return kInvalid; }
        std::lock_guard lifecycle(lifecycle_);
        std::unique_lock lock(mutex_);
        auto allocation = allocations_.upper_bound(static_cast<std::uint64_t>(physical));
        if (allocation == allocations_.begin()) { return kAccess; }
        --allocation;
        const auto offset = static_cast<std::uint64_t>(physical) - allocation->first;
        if (offset >= allocation->second.size || size > allocation->second.size - offset) { return kAccess; }
        if (requested && (requested < kLowest || requested >= kLimit || size > kLimit - requested)) {
            if (flags & kFixed) { return kNoMemory; }
        }
        if (flags & kFixed) {
            for (const auto& [base, mapping] : maps_) {
                if (requested < base + mapping.size && base < requested + size) {
                    // Replacement/splitting needs a transaction over owned views.
                    // Never unmap an unrelated Windows allocation to honor MAP_FIXED.
                    return (flags & kNoOverwrite) ? kExists : kUnsupported;
                }
            }
        }
        void* placeholder = nullptr;
        if (requested >= kLowest && requested < kLimit && !(requested % alignment) && size <= kLimit - requested) {
            placeholder = Reserve(requested, size, alignment);
        }
        if (!placeholder && !(flags & kFixed)) { placeholder = Reserve(0, size, alignment); }
        if (!placeholder) { HostError("reserve_view"); return kNoMemory; }
        const auto base = reinterpret_cast<std::uintptr_t>(placeholder);
        // Allocate bookkeeping before acquiring the mapped-view resource.
        try { maps_.emplace(base, Mapping{size, static_cast<std::uint64_t>(physical), allocation->first, protection}); }
        catch (...) { VirtualFree(placeholder, 0, MEM_RELEASE); throw; }
        void* view = map_(allocation->second.section, GetCurrentProcess(), placeholder, offset,
                         static_cast<SIZE_T>(size), MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
        if (!view) {
            HostError("replace_placeholder"); VirtualFree(placeholder, 0, MEM_RELEASE); maps_.erase(base); return kNoMemory;
        }
        if (!VirtualAlloc(view, static_cast<SIZE_T>(size), MEM_COMMIT, PAGE_READWRITE)) {
            HostError("commit_view"); UnmapViewOfFile(view); maps_.erase(base); return kNoMemory;
        }
        DWORD previous{};
        if (CpuProtection(protection) != PAGE_READWRITE &&
            !VirtualProtect(view, static_cast<SIZE_T>(size), CpuProtection(protection), &previous)) {
            HostError("protect_view"); UnmapViewOfFile(view); maps_.erase(base); return kNoMemory;
        }
        try {
            maps_.at(base).watched = WriteTracker().AddView(view, size, static_cast<std::uint64_t>(physical), allocation->second.identity, CpuProtection(protection));
            if (gpu_observer_) { ApplyGuardsLocked(allocation->first, static_cast<std::uint64_t>(physical), size); }
        }
        catch (...) { UnmapViewOfFile(view); maps_.erase(base); throw; }
        const auto observer = gpu_observer_;
        const auto mapped = InfoLocked(base, maps_.at(base));
        lock.unlock();
        if (observer) { observer->Mapped(mapped); }
        *address = view;
        std::printf("{\"event\":\"direct_memory_map\",\"address\":\"0x%llx\",\"physical\":\"0x%llx\",\"size\":%llu,\"alignment\":%llu,\"protection\":%d,\"flags\":%d}\n", base, static_cast<std::uint64_t>(physical), size, alignment, protection, flags);
        return 0;
    }

    int Unmap(void* address, std::uint64_t size) {
        const auto base = reinterpret_cast<std::uintptr_t>(address);
        if (!PageRange(base, size) || base >= kLimit || size > kLimit - base) { return kInvalid; }
        std::lock_guard lifecycle(lifecycle_);
        std::unique_lock lock(mutex_);
        const auto iterator = maps_.find(base);
        if (iterator == maps_.end() || iterator->second.size != size) {
            for (const auto& [existing, mapping] : maps_) {
                if (base < existing + mapping.size && existing < base + size) { return kUnsupported; }
            }
            return kInvalid;
        }
        if (iterator->second.pins) { return kUnsupported; }
        const auto observer = gpu_observer_;
        const auto old = InfoLocked(base, iterator->second);
        const auto allocation_key = iterator->second.allocation;
        std::map<std::uintptr_t, std::uint64_t> free_node;
        free_node.emplace(base, size);
        lock.unlock();
        if (observer) { observer->BeforeChange(std::span{&old, 1}); }
        lock.lock();
        WriteTracker().RemoveView(iterator->second.watched);
        if (!unmap_(GetCurrentProcess(), address, MEM_PRESERVE_PLACEHOLDER)) {
            iterator->second.watched = WriteTracker().AddView(address, size, iterator->second.physical,
                allocations_.at(iterator->second.allocation).identity, CpuProtection(iterator->second.protection));
            HostError("unmap_view"); lock.unlock();
            if (observer) { observer->Mapped(old); }
            return kInvalid;
        }
        free_placeholders_.insert(free_node.extract(base));
        RemoveGuardsLocked(iterator->second);
        maps_.erase(iterator);
        if (observer) { ApplyGuardsLocked(allocation_key, old.physical, size); }
        TrimFreePlaceholdersLocked();
        return 0;
    }

    int Release(std::uint64_t physical, std::uint64_t size) {
        if (physical % kPage || size % kPage) { return kInvalid; }
        if (!size) { return 0; }
        if (physical >= kCapacity || size > kCapacity - physical) { return kInvalid; }
        std::lock_guard lifecycle(lifecycle_);
        std::unique_lock lock(mutex_);
        auto iterator = allocations_.find(physical);
        if (iterator == allocations_.end() || iterator->second.size != size) {
            for (const auto& [existing, allocation] : allocations_) {
                if (physical < existing + allocation.size && existing < physical + size) { return kUnsupported; }
            }
            return 0; // unchecked ReleaseDirectMemory of an already free range
        }
        for (const auto& [base, mapping] : maps_) {
            (void)base;
            if (mapping.allocation == physical && mapping.pins) { return kUnsupported; }
        }
        const auto observer = gpu_observer_;
        std::vector<NativeMemoryRangeInfo> old;
        std::map<std::uintptr_t, std::uint64_t> free_nodes;
        for (const auto& [base, mapping] : maps_) { if (mapping.allocation == physical) { free_nodes.emplace(base, mapping.size); } }
        if (observer) {
            for (const auto& [base, mapping] : maps_) { if (mapping.allocation == physical) { old.push_back(InfoLocked(base, mapping)); } }
            lock.unlock(); observer->BeforeChange(old); lock.lock();
        }
        for (auto view = maps_.begin(); view != maps_.end();) {
            if (view->second.allocation == physical) {
                WriteTracker().RemoveView(view->second.watched);
                if (!unmap_(GetCurrentProcess(), reinterpret_cast<void*>(view->first), MEM_PRESERVE_PLACEHOLDER)) {
                    view->second.watched = WriteTracker().AddView(reinterpret_cast<void*>(view->first), view->second.size,
                        view->second.physical, iterator->second.identity, CpuProtection(view->second.protection));
                    HostError("release_view");
                    std::vector<NativeMemoryRangeInfo> remaining;
                    if (observer) { for (const auto& [base, mapping] : maps_) { if (mapping.allocation == physical) { remaining.push_back(InfoLocked(base, mapping)); } } }
                    lock.unlock();
                    if (observer) { for (const auto& mapping : remaining) { observer->Mapped(mapping); } }
                    return kInvalid;
                }
                free_placeholders_.insert(free_nodes.extract(view->first));
                RemoveGuardsLocked(view->second);
                view = maps_.erase(view);
            } else { ++view; }
        }
        allocations_.erase(iterator);
        TrimFreePlaceholdersLocked();
        return 0;
    }

    int Protect(void* address, std::uint64_t size, int protection) {
        const auto base = reinterpret_cast<std::uintptr_t>(address);
        if (!PageRange(base, size) || (static_cast<unsigned>(protection) & ~0x37u)) { return kInvalid; }
        if (protection & 4) { return kAccess; }
        std::lock_guard lifecycle(lifecycle_);
        std::unique_lock lock(mutex_);
        const auto iterator = maps_.find(base);
        if (iterator == maps_.end() || iterator->second.size != size) {
            for (const auto& [existing, mapping] : maps_) {
                if (base >= existing && base - existing < mapping.size) { return kUnsupported; }
            }
            return kNotFound;
        }
        unsigned retained{};
        for (unsigned bit = 0; bit < 6; ++bit) { if (iterator->second.required[bit]) { retained |= 1u << bit; } }
        if ((static_cast<unsigned>(protection) & retained) != retained) { return kUnsupported; }
        const auto observer = gpu_observer_;
        const auto old = InfoLocked(base, iterator->second);
        if (observer) { lock.unlock(); observer->BeforeChange(std::span{&old, 1}); lock.lock(); }
        DWORD previous{};
        const bool protected_now = observer ? true : iterator->second.watched
            ? WriteTracker().ProtectView(iterator->second.watched, CpuProtection(protection))
            : VirtualProtect(address, static_cast<SIZE_T>(size), CpuProtection(protection), &previous) != 0;
        if (!protected_now) { HostError("protect_view"); return kInvalid; }
        iterator->second.protection = protection;
        if (observer) {
            ApplyGuardsLocked(iterator->second.allocation, iterator->second.physical, size);
            const auto updated = InfoLocked(base, iterator->second);
            lock.unlock(); observer->Mapped(updated);
        }
        return 0;
    }

    bool Empty() { std::lock_guard lock(mutex_); return allocations_.empty() && maps_.empty(); }

    struct HostWritePin {
        DirectMemory* owner{};
        std::vector<std::uintptr_t> mappings;
        std::vector<NativeMemoryRangeInfo> ranges;
        kt::memory::WriteTracker::HostWrite write;
        std::shared_ptr<NativeGpuMemoryObserver> observer;
        std::uint64_t address{}, bytes{};
        ~HostWritePin() {
            write = {}; // Finish invalidation/exclusion before releasing pins.
            if (owner && observer) { owner->EndGpuHostWrite(*this); }
            else if (owner) { for (const auto base : mappings) { owner->Drop(reinterpret_cast<void*>(base), 2); } }
        }
    };
    std::shared_ptr<void> BeginHostWrite(void* address, std::uint64_t bytes) {
        const auto base = reinterpret_cast<std::uintptr_t>(address);
        if (bytes && (!base || bytes > UINTPTR_MAX - base)) { return {}; }
        auto pin = std::make_shared<HostWritePin>();
        std::lock_guard lifecycle(lifecycle_);
        std::unique_lock lock(mutex_);
        const auto end = base + bytes;
        auto iterator = maps_.lower_bound(base);
        if (iterator != maps_.begin()) {
            const auto previous = std::prev(iterator);
            if (base - previous->first < previous->second.size) { iterator = previous; }
        }
        for (; bytes && iterator != maps_.end() && iterator->first < end; ++iterator) {
            if (!(iterator->second.protection & 2)) { return {}; }
            pin->mappings.push_back(iterator->first);
            auto info = InfoLocked(iterator->first, iterator->second);
            info.address = (std::max)(base, iterator->first);
            info.bytes = (std::min)(end, iterator->first + iterator->second.size) - info.address;
            info.physical += info.address - iterator->first;
            pin->ranges.push_back(info);
        }
        if (gpu_observer_) {
            if (!WritableLocked(base, bytes)) { return {}; }
            pin->observer = gpu_observer_; pin->address = base; pin->bytes = bytes;
            lock.unlock();
            if (!pin->ranges.empty()) { pin->observer->BeforeHostWrite(pin->ranges); }
            lock.lock();
            // Reserve all guard entries before changing counters/pins.
            HostPagesLocked(*pin, [](auto& allocation, auto physical) { allocation.guards.try_emplace(physical); });
            HostPagesLocked(*pin, [](auto& allocation, auto physical) { ++allocation.guards.at(physical).host_writes; });
            for (const auto& range : pin->ranges) {
                const auto begin = range.physical & ~(kHostPage - 1);
                ApplyGuardsLocked(maps_.at(range.mapping_address).allocation, begin, RoundUp(range.physical + range.bytes, kHostPage) - begin);
            }
            ++gpu_host_writes_;
        } else {
            pin->write = WriteTracker().BeginHostWrite(address, bytes);
            if (!pin->write) { return {}; }
        }
        // All allocations/validation precede pin publication. Failure cannot
        // leave a partial pin or invoke Drop while holding this mutex.
        for (const auto key : pin->mappings) { ++maps_.at(key).pins; ++maps_.at(key).required[1]; }
        pin->owner = this;
        return pin;
    }

    bool Writable(const void* address, std::uint64_t bytes) {
        std::lock_guard lock(mutex_);
        return gpu_observer_ ? WritableLocked(reinterpret_cast<std::uintptr_t>(address), bytes) : WriteTracker().Writable(address, bytes);
    }

    bool AttachGpu(std::shared_ptr<NativeGpuMemoryObserver> observer) {
        if (!observer) { return false; }
        std::lock_guard lifecycle(lifecycle_);
        std::unique_lock lock(mutex_);
        if (gpu_observer_) { return false; }
        for (const auto& [base, mapping] : maps_) { (void)base; if (mapping.pins) { return false; } }
        std::vector<NativeMemoryRangeInfo> existing;
        for (const auto& [base, mapping] : maps_) { existing.push_back(InfoLocked(base, mapping)); }
        WriteTracker().Disable();
        for (auto& [base, mapping] : maps_) {
            (void)base; WriteTracker().RemoveView(mapping.watched); mapping.watched.reset();
        }
        gpu_observer_ = observer;
        lock.unlock();
        try { for (const auto& mapping : existing) { observer->Mapped(mapping); } }
        catch (...) {
            // Callers must tear down a partially constructed cache after failure.
            // Keep attachment/guards intact until its explicit Drain/detach.
            throw;
        }
        return true;
    }
    void DetachGpu(const NativeGpuMemoryObserver* observer) {
        std::lock_guard lifecycle(lifecycle_);
        std::unique_lock lock(mutex_);
        if (!observer || gpu_observer_.get() != observer) { throw std::logic_error("GPU memory detach owner mismatch"); }
        host_writes_done_.wait(lock, [this] { return gpu_host_writes_ == 0; });
        auto held = gpu_observer_;
        lock.unlock(); held->Drain(); lock.lock();
        for (auto& [base, mapping] : maps_) { (void)base; RemoveGuardsLocked(mapping); }
        for (auto& [physical, allocation] : allocations_) {
            ApplyGuardsLocked(physical, physical, allocation.size);
            allocation.guards.clear();
        }
        gpu_observer_.reset();
    }
    bool Query(std::uint64_t address, std::uint64_t bytes, NativeMemoryRangeInfo* output) {
        if (!output || !bytes || bytes > UINT64_MAX - address) { return false; }
        std::lock_guard lock(mutex_);
        auto iterator = maps_.upper_bound(address);
        if (iterator == maps_.begin()) { return false; }
        --iterator;
        const auto offset = address - iterator->first;
        if (offset >= iterator->second.size || bytes > iterator->second.size - offset) { return false; }
        auto result = InfoLocked(iterator->first, iterator->second);
        result.address = address; result.bytes = bytes; result.physical += offset;
        *output = result; return true;
    }
    std::vector<NativeMemoryRangeInfo> Aliases(std::uint64_t address, std::uint64_t bytes) {
        std::vector<NativeMemoryRangeInfo> result;
        if (!bytes || bytes > UINT64_MAX - address) { return result; }
        std::lock_guard lock(mutex_);
        auto source = maps_.upper_bound(address);
        if (source == maps_.begin()) { return result; }
        --source;
        const auto offset = address - source->first;
        if (offset >= source->second.size || bytes > source->second.size - offset) { return result; }
        const auto physical = source->second.physical + offset;
        for (const auto& [base, mapping] : maps_) {
            if (mapping.allocation != source->second.allocation) { continue; }
            const auto begin = (std::max)(physical, mapping.physical);
            const auto end = (std::min)(physical + bytes, mapping.physical + mapping.size);
            if (begin >= end) { continue; }
            auto info = InfoLocked(base, mapping);
            info.address = base + begin - mapping.physical; info.physical = begin; info.bytes = end - begin;
            result.push_back(info);
        }
        return result;
    }
    bool GpuCopy(std::uint64_t address, void* data, std::uint64_t bytes, bool write) {
        if (bytes && (!data || bytes > UINT64_MAX - address)) { return false; }
        std::lock_guard lock(mutex_);
        if (!gpu_observer_) { return false; }
        struct Part { Allocation* allocation; std::uint64_t offset, bytes; };
        std::vector<Part> parts;
        const auto end = address + bytes;
        for (auto cursor = address; cursor < end;) {
            auto it = maps_.upper_bound(cursor);
            const bool covered = it != maps_.begin() && cursor - std::prev(it)->first < std::prev(it)->second.size;
            if (!covered) {
                if (write) { return false; }
                const auto stop = it == maps_.end() ? end : (std::min)(end, it->first);
                parts.push_back({nullptr, 0, stop - cursor}); cursor = stop; continue;
            }
            --it; const auto& mapping = it->second;
            if (!(mapping.protection & (write ? 0x20 : 0x10))) { return false; }
            const auto count = (std::min)(end - cursor, mapping.size - (cursor - it->first));
            auto& allocation = allocations_.at(mapping.allocation);
            if (!allocation.backing) {
                allocation.backing = MapViewOfFile(allocation.section, FILE_MAP_ALL_ACCESS, 0, 0, 0);
                if (!allocation.backing) { throw std::runtime_error("Cannot map native GPU section backing"); }
            }
            parts.push_back({&allocation, mapping.physical - mapping.allocation + cursor - it->first, count});
            cursor += count;
        }
        auto* output = static_cast<unsigned char*>(data);
        for (const auto& part : parts) {
            if (!part.allocation) { std::memset(output, 0, static_cast<std::size_t>(part.bytes)); }
            else {
                auto* backing = static_cast<unsigned char*>(part.allocation->backing) + part.offset;
                if (write) { std::memcpy(backing, output, static_cast<std::size_t>(part.bytes)); }
                else { std::memcpy(output, backing, static_cast<std::size_t>(part.bytes)); }
            }
            output += part.bytes;
        }
        return true;
    }
    bool GpuProtect(std::uint64_t address, std::uint64_t bytes, unsigned permissions) {
        if (!bytes || address % kHostPage || bytes % kHostPage || bytes > UINT64_MAX - address || permissions > 3 || permissions == 2) { return false; }
        std::lock_guard lock(mutex_);
        if (!gpu_observer_) { return false; }
        // Cache blocks can straddle several native views and unmapped holes.
        // Holes need no Windows protection. Mapping publication must invalidate
        // the newly mapped GPU range before it can reuse a cached zero-filled
        // hole (the GPU observer's Mapped contract).
        struct Part { Mapping* mapping; std::uint64_t offset, bytes; };
        std::vector<Part> parts;
        auto it = maps_.upper_bound(address);
        if (it != maps_.begin()) { --it; }
        const auto end = address + bytes;
        for (; it != maps_.end() && it->first < end; ++it) {
            auto& mapping = it->second;
            const auto begin = (std::max)(address, it->first);
            const auto stop = (std::min)(end, it->first + mapping.size);
            if (begin < stop) { parts.push_back({&mapping, begin - it->first, stop - begin}); }
        }
        for (const auto& part : parts) {
            auto& mapping = *part.mapping; auto& allocation = allocations_.at(mapping.allocation);
            for (auto page = part.offset; page < part.offset + part.bytes; page += kHostPage) {
                allocation.guards.try_emplace(mapping.physical + page);
                mapping.gpu_permissions.try_emplace(page, 3);
            }
        }
        for (const auto& part : parts) {
            auto& mapping = *part.mapping; auto& allocation = allocations_.at(mapping.allocation);
            for (auto page = part.offset; page < part.offset + part.bytes; page += kHostPage) {
                auto& old = mapping.gpu_permissions.at(page);
                auto& guard = allocation.guards.at(mapping.physical + page);
                guard.read -= !(old & 1); guard.write -= !(old & 2);
                guard.read += !(permissions & 1); guard.write += !(permissions & 2);
                old = permissions;
                if (permissions == 3) { mapping.gpu_permissions.erase(page); }
            }
            ApplyGuardsLocked(mapping.allocation, mapping.physical + part.offset, part.bytes);
        }
        return true;
    }
    bool GpuFault(std::uint64_t address, bool write) {
        std::lock_guard lock(mutex_);
        if (!gpu_observer_) { return false; }
        auto it = maps_.upper_bound(address);
        if (it == maps_.begin()) { return false; }
        --it; const auto& mapping = it->second; const auto offset = address - it->first;
        if (offset >= mapping.size || !(mapping.protection & (write ? 2 : 3))) { return false; }
        const auto physical = (mapping.physical + offset) & ~(kHostPage - 1);
        const auto& guards = allocations_.at(mapping.allocation).guards;
        const auto guard = guards.find(physical);
        return guard != guards.end() && !guard->second.host_writes && (write ? guard->second.write || guard->second.read : guard->second.read);
    }

    kt::memory::WriteStamp Track(void* address, std::uint64_t size) {
        const auto base = reinterpret_cast<std::uintptr_t>(address);
        std::lock_guard lock(mutex_);
        auto iterator = maps_.upper_bound(base);
        if (!size || iterator == maps_.begin()) { return {}; }
        --iterator; const auto offset = base - iterator->first;
        if (offset >= iterator->second.size || size > iterator->second.size - offset || !(iterator->second.protection & 1)) { return {}; }
        return WriteTracker().Track(allocations_.at(iterator->second.allocation).identity, iterator->second.physical + offset, size);
    }

    bool Retain(void* address, std::uint64_t size, std::uint64_t* physical_out, unsigned required,
                NativeMemoryRangeInfo* info_out = nullptr) {
        const auto base = reinterpret_cast<std::uintptr_t>(address);
        if (!size || !physical_out || !required || (required & ~0x33u)) { return false; }
        std::lock_guard lifecycle(lifecycle_);
        std::lock_guard lock(mutex_);
        auto iterator = maps_.upper_bound(base);
        if (iterator == maps_.begin()) { return false; }
        --iterator;
        const auto offset = base - iterator->first;
        if (offset >= iterator->second.size || size > iterator->second.size - offset || (static_cast<unsigned>(iterator->second.protection) & required) != required) { return false; }
        ++iterator->second.pins;
        for (unsigned bit = 0; bit < 6; ++bit) { if (required & (1u << bit)) { ++iterator->second.required[bit]; } }
        *physical_out = iterator->second.physical + offset;
        if (info_out) {
            *info_out = {base, size, *physical_out, allocations_.at(iterator->second.allocation).identity,
                iterator->first, iterator->second.size, static_cast<unsigned>(iterator->second.protection), required};
        }
        return true;
    }
    void Drop(void* address, unsigned required) {
        const auto base = reinterpret_cast<std::uintptr_t>(address);
        std::lock_guard lock(mutex_);
        auto iterator = maps_.upper_bound(base);
        if (iterator == maps_.begin()) { UnsupportedApi("native_memory_invalid_lease", base, nullptr); }
        --iterator;
        if (base - iterator->first >= iterator->second.size || !iterator->second.pins || !required || (required & ~0x33u)) {
            UnsupportedApi("native_memory_invalid_lease", base, nullptr);
        }
        for (unsigned bit = 0; bit < 6; ++bit) {
            if ((required & (1u << bit)) && !iterator->second.required[bit]) { UnsupportedApi("native_memory_invalid_lease_protection", required, nullptr); }
        }
        --iterator->second.pins;
        for (unsigned bit = 0; bit < 6; ++bit) { if (required & (1u << bit)) { --iterator->second.required[bit]; } }
    }

private:
    using AllocateFunction = PVOID(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
    using MapFunction = PVOID(WINAPI*)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
    using UnmapFunction = BOOL(WINAPI*)(HANDLE, PVOID, ULONG);
    AllocateFunction allocate_{};
    MapFunction map_{};
    UnmapFunction unmap_{};
    std::mutex mutex_;
    std::mutex lifecycle_;
    std::condition_variable host_writes_done_;
    std::shared_ptr<NativeGpuMemoryObserver> gpu_observer_;
    std::uint64_t gpu_host_writes_{};
    std::map<std::uint64_t, Allocation> allocations_;
    std::map<std::uintptr_t, Mapping> maps_;
    // Unoccupied, OWNED Windows placeholders. Keep 16-KiB fragments so an
    // adjacent view does not need to reserve an overlapping 64-KiB envelope.
    std::map<std::uintptr_t, std::uint64_t> free_placeholders_;

    void TrimFreePlaceholdersLocked() {
        for (auto it = free_placeholders_.begin(); it != free_placeholders_.end();) {
            const auto begin = it->first & ~(kGranularity - 1);
            const auto end = RoundUp(it->first + it->second, kGranularity);
            bool needed{};
            for (const auto& [base, mapping] : maps_) {
                if (base < end && begin < base + mapping.size) { needed = true; break; }
            }
            if (needed) { ++it; continue; }
            if (!VirtualFree(reinterpret_cast<void*>(it->first), 0, MEM_RELEASE)) { throw std::runtime_error("Cannot retire native free placeholder"); }
            it = free_placeholders_.erase(it);
        }
    }

    NativeMemoryRangeInfo InfoLocked(std::uintptr_t base, const Mapping& mapping) {
        return {base, mapping.size, mapping.physical, allocations_.at(mapping.allocation).identity,
            base, mapping.size, static_cast<unsigned>(mapping.protection), 0};
    }
    bool WritableLocked(std::uintptr_t address, std::uint64_t bytes) {
        if (bytes && (!address || bytes > UINTPTR_MAX - address)) { return false; }
        const auto end = address + bytes;
        for (auto cursor = address; cursor < end;) {
            auto it = maps_.upper_bound(cursor);
            if (it != maps_.begin() && cursor - std::prev(it)->first < std::prev(it)->second.size) {
                --it; if (!(it->second.protection & 2)) { return false; }
                cursor += (std::min)(end - cursor, it->second.size - (cursor - it->first)); continue;
            }
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD)) { return false; }
            const auto rights = info.Protect & 0xff;
            if (rights != PAGE_READWRITE && rights != PAGE_WRITECOPY && rights != PAGE_EXECUTE_READWRITE && rights != PAGE_EXECUTE_WRITECOPY) { return false; }
            auto stop = (std::min)(end, reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize);
            if (it != maps_.end()) { stop = (std::min)(stop, it->first); }
            if (stop <= cursor) { return false; } cursor = stop;
        }
        return true;
    }
    void ApplyGuardsLocked(std::uint64_t key, std::uint64_t physical, std::uint64_t bytes) {
        const auto& allocation = allocations_.at(key);
        for (const auto& [base, mapping] : maps_) {
            if (mapping.allocation != key) { continue; }
            const auto begin = (std::max)(physical, mapping.physical);
            const auto end = (std::min)(physical + bytes, mapping.physical + mapping.size);
            std::uint64_t run{}; DWORD last{};
            const auto protect = [&](std::uint64_t stop) {
                if (stop == run) { return; }
                DWORD previous{};
                if (!VirtualProtect(reinterpret_cast<void*>(base + run - mapping.physical), static_cast<SIZE_T>(stop - run), last, &previous)) {
                    throw std::runtime_error("Cannot apply native GPU cache page guards");
                }
            };
            for (auto p = begin; p < end; p += kHostPage) {
                const auto guard = allocation.guards.find(p);
                unsigned allowed = (mapping.protection & 2) ? 3 : (mapping.protection & 1);
                if (guard != allocation.guards.end() && !guard->second.host_writes) {
                    if (guard->second.read) { allowed = 0; }
                    else if (guard->second.write) { allowed &= 1; }
                }
                const auto protection = CpuProtection(static_cast<int>(allowed));
                if (p == begin) { run = p; last = protection; }
                else if (protection != last) { protect(p); run = p; last = protection; }
            }
            if (begin < end) { protect(end); }
        }
    }
    void RemoveGuardsLocked(Mapping& mapping) {
        auto& allocation = allocations_.at(mapping.allocation);
        for (const auto& [offset, permissions] : mapping.gpu_permissions) {
            auto& guard = allocation.guards.at(mapping.physical + offset);
            guard.read -= !(permissions & 1); guard.write -= !(permissions & 2);
        }
        mapping.gpu_permissions.clear();
    }
    template <typename Function> void HostPagesLocked(const HostWritePin& pin, Function operation) {
        for (const auto key : pin.mappings) {
            const auto& mapping = maps_.at(key); auto& allocation = allocations_.at(mapping.allocation);
            const auto begin = (std::max)(pin.address, key) & ~(kHostPage - 1);
            const auto end = RoundUp((std::min)(pin.address + pin.bytes, key + mapping.size), kHostPage);
            for (auto page = begin; page < end; page += kHostPage) { operation(allocation, mapping.physical + page - key); }
        }
    }
    void EndGpuHostWrite(const HostWritePin& pin) noexcept {
        try {
            if (!pin.ranges.empty()) { pin.observer->AfterHostWrite(pin.ranges); }
            std::lock_guard lock(mutex_);
            HostPagesLocked(pin, [](auto& allocation, auto physical) { --allocation.guards.at(physical).host_writes; });
            for (const auto key : pin.mappings) {
                auto& mapping = maps_.at(key);
                --mapping.pins; --mapping.required[1];
            }
            for (const auto& range : pin.ranges) {
                const auto begin = range.physical & ~(kHostPage - 1);
                ApplyGuardsLocked(maps_.at(range.mapping_address).allocation, begin, RoundUp(range.physical + range.bytes, kHostPage) - begin);
            }
            --gpu_host_writes_; host_writes_done_.notify_all();
        } catch (...) { std::terminate(); }
    }

    static void HostError(const char* operation) {
        const auto error = GetLastError();
        std::printf("{\"event\":\"direct_memory_host_error\",\"operation\":\"%s\",\"win32_error\":%lu}\n", operation, error);
    }

    void* Reserve(std::uintptr_t requested, std::uint64_t size, std::uint64_t alignment) {
        for (auto it = free_placeholders_.begin(); it != free_placeholders_.end();) {
            auto next = std::next(it);
            if (next != free_placeholders_.end() && it->first + it->second == next->first) {
                if (!VirtualFree(reinterpret_cast<void*>(it->first), static_cast<SIZE_T>(it->second + next->second), MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS)) {
                    throw std::runtime_error("Cannot coalesce native free placeholders");
                }
                it->second += next->second; free_placeholders_.erase(next);
            } else { ++it; }
        }
        for (auto it = free_placeholders_.begin(); it != free_placeholders_.end(); ++it) {
            const auto candidate = requested ? requested : RoundUp(it->first, alignment);
            if (candidate < it->first || candidate - it->first >= it->second || size > it->second - (candidate - it->first)) { continue; }
            const auto base = it->first, extent = it->second;
            free_placeholders_.erase(it);
            return PartitionPlaceholder(base, extent, candidate, size);
        }
        const auto prefix = requested % kGranularity;
        const auto extent = RoundUp(prefix + size, kGranularity);
        if (requested) {
            // No fitting placeholder: remove ONLY our own free reservations in
            // this envelope. Existing mapped or unrelated Windows views stay.
            const auto begin = requested - prefix, end = begin + extent;
            for (auto it = free_placeholders_.begin(); it != free_placeholders_.end();) {
                if (it->first < end && begin < it->first + it->second) {
                    if (!VirtualFree(reinterpret_cast<void*>(it->first), 0, MEM_RELEASE)) { throw std::runtime_error("Cannot release native free placeholder"); }
                    it = free_placeholders_.erase(it);
                } else { ++it; }
            }
        }
        MEM_ADDRESS_REQUIREMENTS requirements{};
        requirements.LowestStartingAddress = reinterpret_cast<void*>(kLowest);
        requirements.HighestEndingAddress = reinterpret_cast<void*>(kLimit - 1);
        requirements.Alignment = static_cast<SIZE_T>((std::max)(alignment, kGranularity));
        MEM_EXTENDED_PARAMETER parameter{};
        parameter.Type = MemExtendedParameterAddressRequirements;
        parameter.Pointer = &requirements;
        auto* reserved = allocate_(GetCurrentProcess(),
            requested ? reinterpret_cast<void*>(requested - prefix) : nullptr, static_cast<SIZE_T>(extent),
            MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS,
            requested ? nullptr : &parameter, requested ? 0 : 1);
        if (!reserved) { return nullptr; }
        const auto base = reinterpret_cast<std::uintptr_t>(reserved);
        return PartitionPlaceholder(base, extent, base + prefix, size);
    }
    void* PartitionPlaceholder(std::uintptr_t base, std::uint64_t extent, std::uintptr_t requested, std::uint64_t size) {
        const auto prefix = requested - base;
        const auto tail = extent - prefix - size;
        std::map<std::uintptr_t, std::uint64_t> fragments;
        try {
            if (prefix) { fragments.emplace(base, prefix); }
            if (tail) { fragments.emplace(requested + size, tail); }
        } catch (...) { VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE); throw; }
        if (prefix) {
            if (!VirtualFree(reinterpret_cast<void*>(base), static_cast<SIZE_T>(prefix), MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
                VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE); return nullptr;
            }
            free_placeholders_.insert(fragments.extract(base));
        }
        if (tail) {
            if (!VirtualFree(reinterpret_cast<void*>(requested), static_cast<SIZE_T>(size), MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
                VirtualFree(reinterpret_cast<void*>(requested), 0, MEM_RELEASE); return nullptr;
            }
            free_placeholders_.insert(fragments.extract(requested + size));
        }
        return reinterpret_cast<void*>(requested);
    }
};

DirectMemory& Memory() { static DirectMemory memory; return memory; }

template <typename Function>
int Api(const char* name, const void* caller, Function operation) {
    int result{};
    try { result = operation(); }
    catch (const std::bad_alloc&) { result = kNoMemory; }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Native direct memory error: %s\n", error.what());
        UnsupportedApi("native_direct_memory_host", GetLastError(), caller);
    }
    if (result == kUnsupported) { UnsupportedApi(name, static_cast<unsigned>(result), caller); }
    if (result) { std::printf("{\"event\":\"direct_memory_error\",\"name\":\"%s\",\"result\":\"0x%08x\"}\n", name, static_cast<unsigned>(result)); }
    return result;
}

std::uint64_t KT_SYSV Capacity() {
    TraceNativeApi("sceKernelGetDirectMemorySize", kCapacity, __builtin_return_address(0));
    return kCapacity;
}
int KT_SYSV Allocate(std::int64_t start, std::int64_t end, std::uint64_t size, std::uint64_t alignment, int type, std::int64_t* output) {
    const auto caller = __builtin_return_address(0);
    TraceNativeApi("sceKernelAllocateDirectMemory", size, caller);
    return Api("sceKernelAllocateDirectMemory", caller, [&] { return Memory().Allocate(start, end, size, alignment, type, output); });
}
int KT_SYSV Map(void** address, std::uint64_t size, int protection, int flags, std::int64_t physical, std::uint64_t alignment) {
    const auto caller = __builtin_return_address(0);
    TraceNativeApi("sceKernelMapDirectMemory", size, caller);
    return Api("sceKernelMapDirectMemory", caller, [&] { return Memory().Map(address, size, protection, flags, physical, alignment); });
}
int KT_SYSV MapNamed(void** address, std::uint64_t size, int protection, int flags, std::int64_t physical, std::uint64_t alignment, const char* name) {
    const auto caller = __builtin_return_address(0);
    return Api("sceKernelMapNamedDirectMemory", caller, [&] {
        if (!name) { return kInvalid; }
        if (strnlen_s(name, 32) == 32) { return kNameTooLong; }
        return Memory().Map(address, size, protection, flags, physical, alignment);
    });
}
int KT_SYSV Unmap(void* address, std::uint64_t size) {
    const auto caller = __builtin_return_address(0);
    return Api("sceKernelMunmap", caller, [&] { return Memory().Unmap(address, size); });
}
int KT_SYSV Release(std::uint64_t physical, std::uint64_t size) {
    const auto caller = __builtin_return_address(0);
    return Api("sceKernelReleaseDirectMemory", caller, [&] { return Memory().Release(physical, size); });
}
int KT_SYSV Protect(void* address, std::uint64_t size, int protection) {
    const auto caller = __builtin_return_address(0);
    return Api("sceKernelMprotect", caller, [&] { return Memory().Protect(address, size, protection); });
}
} // namespace

std::uint64_t ResolveMemoryImport(std::string_view identifier) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "pO96TwzOm5E") { return reinterpret_cast<std::uint64_t>(&Capacity); }
    if (hash == "rTXw65xmLIA") { return reinterpret_cast<std::uint64_t>(&Allocate); }
    if (hash == "L-Q3LEjIbgA") { return reinterpret_cast<std::uint64_t>(&Map); }
    if (hash == "NcaWUxfMNIQ") { return reinterpret_cast<std::uint64_t>(&MapNamed); }
    if (hash == "MBuItvba6z8") { return reinterpret_cast<std::uint64_t>(&Release); }
    if (hash == "cQke9UuBQOk") { return reinterpret_cast<std::uint64_t>(&Unmap); }
    if (hash == "vSMAm3cxYTY") { return reinterpret_cast<std::uint64_t>(&Protect); }
    return 0;
}

bool RetainNativeMemoryRange(void* address, std::uint64_t size, std::uint64_t* physical_out, unsigned required) {
    return Memory().Retain(address, size, physical_out, required);
}
bool RetainNativeMemoryRangeInfo(void* address, std::uint64_t size, NativeMemoryRangeInfo* output, unsigned required) {
    if (!output) { return false; }
    std::uint64_t physical{};
    return Memory().Retain(address, size, &physical, required, output);
}
void ReleaseNativeMemoryRange(void* address, unsigned required) { Memory().Drop(address, required); }
kt::memory::WriteStamp TrackNativeMemoryRange(void* address, std::uint64_t size) { return Memory().Track(address, size); }
bool NativeMemoryWritable(const void* address, std::uint64_t size) { return Memory().Writable(address, size); }
NativeHostWrite BeginNativeHostWrite(void* address, std::uint64_t size) {
    NativeHostWrite result; result.state_ = Memory().BeginHostWrite(address, size); return result;
}
bool CopyToNativeMemory(void* destination, const void* source, std::uint64_t size) {
    if (size && !source) { return false; }
    auto write = BeginNativeHostWrite(destination, size);
    if (!write) { return false; }
    if (size) { std::memcpy(destination, source, static_cast<std::size_t>(size)); }
    return true;
}
bool AttachNativeGpuMemory(std::shared_ptr<NativeGpuMemoryObserver> observer) { return Memory().AttachGpu(std::move(observer)); }
void DetachNativeGpuMemory(const NativeGpuMemoryObserver* observer) { Memory().DetachGpu(observer); }
bool QueryNativeMemoryRange(std::uint64_t address, std::uint64_t bytes, NativeMemoryRangeInfo* output) { return Memory().Query(address, bytes, output); }
std::vector<NativeMemoryRangeInfo> NativeMemoryAliases(std::uint64_t address, std::uint64_t bytes) { return Memory().Aliases(address, bytes); }
bool ReadNativeGpuMemory(std::uint64_t address, void* destination, std::uint64_t bytes) { return Memory().GpuCopy(address, destination, bytes, false); }
bool WriteNativeGpuMemory(std::uint64_t address, const void* source, std::uint64_t bytes) { return Memory().GpuCopy(address, const_cast<void*>(source), bytes, true); }
bool ProtectNativeGpuMemory(std::uint64_t address, std::uint64_t bytes, unsigned permissions) { return Memory().GpuProtect(address, bytes, permissions); }
bool NativeGpuMemoryFault(std::uint64_t address, bool write) { return Memory().GpuFault(address, write); }

bool MemorySelfTest() {
    DirectMemory memory;
    bool passed = true;
    auto check = [&](const char* name, bool result) {
        passed &= result;
        std::printf("{\"event\":\"memory_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    std::int64_t physical = -1, second = -1;
    check("invalid_allocation_preserves_output", memory.Allocate(-1, kCapacity, kPage, kPage, 0, &physical) == kInvalid && physical == -1 &&
        memory.Allocate(0, kCapacity, 1, kPage, 0, &physical) == kInvalid && memory.Allocate(0, kPage, 2 * kPage, 0, 0, &physical) == kAgain &&
        memory.Allocate(0, kCapacity, kPage, 1, 0, &physical) == kInvalid && memory.Allocate(0, kCapacity, kPage, 0, 11, &physical) == kInvalid);
    check("unsupported_memory_type", memory.Allocate(0, kCapacity, kPage, 0, 1, &physical) == kUnsupported);
    if (memory.Allocate(kPage, kCapacity, 4 * kPage, 0x200000, 3, &physical)) { check("allocate", false); return false; }
    check("physical_alignment_and_disjoint_ranges", physical == 0x200000 &&
        memory.Allocate(physical, physical + 4 * kPage, kPage, kPage, 0, &second) == kAgain && second == -1 &&
        memory.Allocate(0, kCapacity, kPage, 3 * kPage, 10, &second) == 0 && second == 0);
    void* first{};
    if (memory.Map(&first, 4 * kPage, 0x33, 0, physical, 0x200000)) { check("map", false); return false; }
    const auto base = reinterpret_cast<std::uintptr_t>(first);
    auto* bytes = static_cast<volatile unsigned char*>(first);
    bool zero = true;
    for (std::size_t i = 0; i < 4 * kPage; ++i) { zero &= bytes[i] == 0; }
    check("bounded_aligned_zeroed_mapping", base >= kLowest && base + 4 * kPage <= kLimit && base % 0x200000 == 0 && zero);
    bytes[kPage] = 0x5a;
    void* alias{};
    if (memory.Map(&alias, kPage, 0x33, 0, physical + kPage, 0x200000)) { check("map_subrange_alias", false); return false; }
    auto* shared = static_cast<volatile unsigned char*>(alias);
    const bool alias_read = shared[0] == 0x5a;
    shared[1] = 0x9b;
    check("unaligned_section_offset_alias_visibility", alias != first && reinterpret_cast<std::uintptr_t>(alias) % 0x200000 == 0 && alias_read && bytes[kPage + 1] == 0x9b);
    void* invalid{};
    check("invalid_mapping_and_execute_rejection", memory.Map(&invalid, kPage, 0x33, 0, physical + 4 * kPage, 0) == kAccess && !invalid &&
        memory.Map(&invalid, kPage, 7, 0, physical, 0) == kAccess && !invalid && memory.Map(nullptr, kPage, 3, 0, physical, 0) == kInvalid);
    void* fixed = first;
    check("no_overwrite_and_explicit_partial_operations", memory.Map(&fixed, kPage, 3, kFixed | kNoOverwrite, physical, 0) == kExists &&
        memory.Unmap(first, kPage) == kUnsupported && memory.Release(physical, kPage) == kUnsupported && bytes[kPage] == 0x5a);
    MEMORY_BASIC_INFORMATION info{};
    check("cpu_read_only_and_no_access_permissions", memory.Protect(alias, kPage, 0x31) == 0 && VirtualQuery(alias, &info, sizeof(info)) && info.Protect == PAGE_READONLY &&
        memory.Protect(alias, kPage, 0x30) == 0 && VirtualQuery(alias, &info, sizeof(info)) && info.Protect == PAGE_NOACCESS && memory.Protect(alias, kPage, 0x33) == 0);
    check("unmap_keeps_backing_and_other_alias", memory.Unmap(alias, kPage) == 0 && bytes[kPage + 1] == 0x9b);
    std::uint64_t retained_physical{};
    const bool retained = memory.Retain(first, 4 * kPage, &retained_physical, 0x30);
    const bool retained_errors = memory.Protect(first, 4 * kPage, 3) == kUnsupported && memory.Unmap(first, 4 * kPage) == kUnsupported &&
        memory.Release(static_cast<std::uint64_t>(physical), 4 * kPage) == kUnsupported;
    if (retained) { memory.Drop(first, 0x30); }
    check("gpu_permission_lease_lifetime", retained && retained_errors && retained_physical == static_cast<std::uint64_t>(physical) &&
        memory.Protect(first, 4 * kPage, 3) == 0 && memory.Protect(first, 4 * kPage, 0x33) == 0);
    fixed = alias;
    fixed = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(alias) + kPage);
    const auto fixed_address = fixed;
    check("fixed_16k_view", memory.Map(&fixed, kPage, 3, kFixed | kNoOverwrite, physical + kPage, 0) == 0 && fixed == fixed_address && static_cast<volatile unsigned char*>(fixed)[1] == 0x9b);
    std::array<void*, 4> adjacent{};
    bool adjacent_mapped = true;
    constexpr std::uintptr_t adjacent_base = 0x6000000000ull;
    for (unsigned i = 0; i < adjacent.size(); ++i) {
        adjacent[i] = reinterpret_cast<void*>(adjacent_base + i * kPage);
        adjacent_mapped &= memory.Map(&adjacent[i], kPage, 3, kFixed | kNoOverwrite, physical + i * kPage, 0) == 0;
    }
    check("four_adjacent_16k_views_share_one_windows_granule", adjacent_mapped);
    if (!adjacent_mapped) { return false; }
    static_cast<volatile unsigned char*>(adjacent[2])[5] = 0xa9;
    check("adjacent_views_keep_distinct_section_offsets", bytes[2 * kPage + 5] == 0xa9 && static_cast<volatile unsigned char*>(adjacent[1])[1] == 0x9b);
    void* joined = adjacent[1];
    check("freed_adjacent_placeholders_coalesce_while_neighbors_stay_mapped", memory.Unmap(adjacent[1], kPage) == 0 &&
        memory.Unmap(adjacent[2], kPage) == 0 && memory.Map(&joined, 2 * kPage, 3, kFixed | kNoOverwrite, physical + kPage, 0) == 0 &&
        joined == adjacent[1] && static_cast<volatile unsigned char*>(joined)[kPage + 5] == 0xa9);
    check("unmapping_joined_views_preserves_neighbor_views", memory.Unmap(joined, 2 * kPage) == 0 &&
        static_cast<volatile unsigned char*>(adjacent[0])[0] == bytes[0] && static_cast<volatile unsigned char*>(adjacent[3])[0] == bytes[3 * kPage]);
    check("last_neighbor_unmap_retires_all_placeholder_fragments", memory.Unmap(adjacent[0], kPage) == 0 &&
        memory.Unmap(adjacent[3], kPage) == 0 && VirtualQuery(reinterpret_cast<void*>(adjacent_base), &info, sizeof(info)) && info.State == MEM_FREE);
    auto watch = memory.Track(reinterpret_cast<void*>(base + kPage + 129), 128);
    NativeMemoryRangeInfo invalid_info{1, 2, 3, 4, 5, 6, 7, 8};
    const auto preserved_info = invalid_info;
    std::uint64_t invalid_physical = 0xdead;
    check("gpu_range_metadata_failure_preserves_outputs", !memory.Retain(first, 4 * kPage + 1, &invalid_physical, 0x11, &invalid_info) &&
        !memory.Retain(fixed, 128, &invalid_physical, 0x11, &invalid_info) && invalid_info == preserved_info && invalid_physical == 0xdead &&
        !RetainNativeMemoryRangeInfo(first, 128, nullptr, 0x11));
    NativeMemoryRangeInfo primary_info{}, alias_info{};
    auto* primary_address = reinterpret_cast<void*>(base + kPage + 129);
    auto* alias_address = static_cast<unsigned char*>(fixed) + 129;
    const bool primary_held = memory.Retain(primary_address, 128, &retained_physical, 0x11, &primary_info);
    const bool alias_held = memory.Retain(alias_address, 128, &retained_physical, 1, &alias_info);
    check("gpu_range_metadata_identifies_physical_aliases_and_exact_bounds", primary_held && alias_held &&
        primary_info.address == base + kPage + 129 && primary_info.bytes == 128 && primary_info.mapping_address == base &&
        primary_info.mapping_bytes == 4 * kPage && primary_info.physical == static_cast<std::uint64_t>(physical) + kPage + 129 &&
        primary_info.allocation_identity != 0 && primary_info.allocation_identity == alias_info.allocation_identity &&
        primary_info.physical == alias_info.physical && alias_info.mapping_bytes == kPage && alias_info.logical_protection == 3);
    check("gpu_range_metadata_preserves_logical_rights_and_permission_pins", primary_held && alias_held &&
        primary_info.logical_protection == 0x33 && primary_info.retained_protection == 0x11 &&
        VirtualQuery(primary_address, &info, sizeof(info)) && info.Protect == PAGE_READONLY &&
        memory.Protect(first, 4 * kPage, 3) == kUnsupported && memory.Unmap(first, 4 * kPage) == kUnsupported &&
        memory.Release(physical, 4 * kPage) == kUnsupported);
    if (primary_held) { memory.Drop(primary_address, 0x11); }
    if (alias_held) { memory.Drop(alias_address, 1); }
    check("direct_memory_tracking_covers_full_and_partial_aliases", watch.Unchanged() &&
        VirtualQuery(fixed, &info, sizeof(info)) && info.Protect == PAGE_READONLY &&
        !memory.Track(nullptr, 16) && !memory.Track(first, 5 * kPage));
    static_cast<volatile unsigned char*>(fixed)[129] = 0xa6;
    check("direct_memory_alias_writes_invalidate_real_backing_proofs", !watch.Unchanged() && bytes[kPage + 129] == 0xa6);
    watch = memory.Track(reinterpret_cast<void*>(base + kPage + 129), 128);
    const bool rw_retained = memory.Retain(first, 4 * kPage, &retained_physical, 3);
    const bool blocked_protect = memory.Protect(first, 4 * kPage, 0x31) == kUnsupported;
    if (rw_retained) { memory.Drop(first, 3); }
    check("tracking_preserves_required_permission_leases", rw_retained && blocked_protect && watch.Unchanged());
    const bool ro = memory.Protect(fixed, kPage, 0x31) == 0;
    const auto ro_watch = memory.Track(fixed, 16);
    const bool none = memory.Protect(fixed, kPage, 0x30) == 0;
    check("guest_protection_changes_invalidate_and_noaccess_cannot_be_tracked", ro && none && !watch.Unchanged() &&
        !ro_watch.Unchanged() && !memory.Track(fixed, 16) && VirtualQuery(fixed, &info, sizeof(info)) && info.Protect == PAGE_NOACCESS);
    const bool rw = memory.Protect(fixed, kPage, 0x33) == 0;
    watch = memory.Track(reinterpret_cast<void*>(base + kPage + 129), 128);
    static_cast<volatile unsigned char*>(fixed)[129] = 0x6a;
    check("restored_writable_alias_is_rearmed_and_observed", rw && !watch.Unchanged() && bytes[kPage + 129] == 0x6a);
    watch = memory.Track(reinterpret_cast<void*>(base + kPage + 129), 128);
    check("host_write_validation_uses_logical_permissions_of_guarded_direct_memory", watch.Unchanged() &&
        NativeMemoryWritable(fixed, kPage) && VirtualQuery(fixed, &info, sizeof(info)) && info.Protect == PAGE_READONLY);
    auto host_write = memory.BeginHostWrite(static_cast<unsigned char*>(fixed) + 129, 8);
    check("host_write_pins_direct_mapping_lifetime_and_required_write_permission", host_write &&
        memory.Unmap(fixed, kPage) == kUnsupported && memory.Release(physical, 4 * kPage) == kUnsupported &&
        memory.Protect(fixed, kPage, 0x31) == kUnsupported);
    const unsigned char host_value = 0x6b;
    std::memcpy(static_cast<unsigned char*>(fixed) + 129, &host_value, 1);
    check("host_write_excludes_tracking_through_all_physical_aliases", !watch.Unchanged() &&
        !memory.Track(reinterpret_cast<void*>(base + kPage + 129), 128) && !memory.Track(fixed, 16) && bytes[kPage + 129] == 0x6b &&
        memory.Track(first, 16).Unchanged());
    host_write.reset();
    check("completed_host_write_releases_permission_pin_and_allows_fresh_tracking", memory.Protect(fixed, kPage, 0x31) == 0 &&
        !memory.BeginHostWrite(fixed, 16) && !NativeMemoryWritable(fixed, 16) && memory.Protect(fixed, kPage, 0x33) == 0 &&
        memory.Track(fixed, 16).Unchanged());
    check("invalid_host_write_request_does_not_leave_direct_memory_pinned", !memory.BeginHostWrite(reinterpret_cast<void*>(UINTPTR_MAX - 1), 8) &&
        !memory.BeginHostWrite(nullptr, 1) && memory.BeginHostWrite(nullptr, 0) && memory.Protect(fixed, kPage, 0x33) == 0);
    const unsigned char copy_input[]{4, 5, 6}; unsigned char copy_output[]{1, 2, 3, 9};
    check("host_copy_uses_owned_source_and_preserves_unwritten_tail", CopyToNativeMemory(copy_output, copy_input, sizeof(copy_input)) &&
        !std::memcmp(copy_output, copy_input, sizeof(copy_input)) && copy_output[3] == 9 && !CopyToNativeMemory(nullptr, copy_input, 1) &&
        !CopyToNativeMemory(copy_output, nullptr, 1) && CopyToNativeMemory(nullptr, nullptr, 0));
    static_cast<volatile unsigned char*>(fixed)[129] = 0x6a;
    watch = memory.Track(reinterpret_cast<void*>(base + kPage + 129), 128);
    const bool watched_unmap = memory.Unmap(fixed, kPage) == 0;
    const bool watched_remap = memory.Map(&fixed, kPage, 3, kFixed | kNoOverwrite, physical + kPage, 0) == 0;
    check("unmap_and_new_alias_registration_invalidate_old_tracking_generation", watched_unmap && watched_remap &&
        !watch.Unchanged() && memory.Track(fixed, 16).Unchanged() && static_cast<volatile unsigned char*>(fixed)[129] == 0x6a);
    // Host-owned memory is not eligible for replacement, even for MAP_FIXED.
    auto* owned = VirtualAlloc(reinterpret_cast<void*>(kLowest + 0x200100000ull), kGranularity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (owned) { static_cast<volatile unsigned char*>(owned)[0] = 0xa7; }
    void* collision = owned;
    const auto collision_result = memory.Map(&collision, kPage, 3, kFixed, physical, 0);
    check("host_allocation_never_replaced", owned && collision_result == kNoMemory && static_cast<volatile unsigned char*>(owned)[0] == 0xa7);
    if (owned) { VirtualFree(owned, 0, MEM_RELEASE); }
    const auto before_release = memory.Track(first, 4096);
    check("release_unmaps_every_alias", memory.Release(physical, 4 * kPage) == 0 && VirtualQuery(first, &info, sizeof(info)) && info.State == MEM_FREE &&
        VirtualQuery(fixed, &info, sizeof(info)) && info.State == MEM_FREE);
    check("release_retires_all_cached_generation_proofs", !before_release.Unchanged() && !memory.Track(first, 4096));
    std::int64_t reused = -1;
    void* fresh{};
    const bool reallocated = memory.Allocate(physical, physical + 4 * kPage, 4 * kPage, kPage, 0, &reused) == 0 && reused == physical &&
        memory.Map(&fresh, 4 * kPage, 3, 0, reused, 0) == 0;
    check("released_range_reuses_with_zeroed_storage", reallocated && static_cast<volatile unsigned char*>(fresh)[kPage] == 0);
    NativeMemoryRangeInfo fresh_info{};
    const bool fresh_held = reallocated && memory.Retain(fresh, 16, &retained_physical, 3, &fresh_info);
    check("gpu_range_metadata_does_not_recycle_allocation_identity", fresh_held && fresh_info.physical == static_cast<std::uint64_t>(physical) &&
        fresh_info.allocation_identity != 0 && fresh_info.allocation_identity != primary_info.allocation_identity);
    if (fresh_held) { memory.Drop(fresh, 3); }
    check("allocation_reuse_cannot_revive_a_previous_backing_proof", reallocated && memory.Track(fresh, 4096).Unchanged() && !before_release.Unchanged());
    check("clean_lifecycle", memory.Release(static_cast<std::uint64_t>(reused), 4 * kPage) == 0 && memory.Release(static_cast<std::uint64_t>(second), kPage) == 0 && memory.Empty());
    check("development_capacity_keeps_high_32_bits", kCapacity == (9ull << 29));
    std::int64_t startup = -1, movie = -1;
    constexpr std::uint64_t startup_size = 0xea800000, movie_size = 0x20000000;
    const auto startup_result = memory.Allocate(0, kCapacity, startup_size, 0x200000, 3, &startup);
    check("startup_footprint_reserves_without_committing_gibibytes", startup_result == 0 && startup == 0);
    const auto movie_result = memory.Allocate(0, kCapacity, movie_size, 0x200000, 0, &movie);
    check("movie_arena_extends_past_four_gibibytes_without_aliasing", movie_result == 0 && movie == startup_size &&
        static_cast<std::uint64_t>(movie) + movie_size > (1ull << 32));
    void* high{};
    const auto high_result = memory.Map(&high, kPage, 3, 0, 1ull << 32, kGranularity);
    if (high_result == 0) { *static_cast<volatile unsigned char*>(high) = 0xa7; }
    check("high_physical_offset_maps_real_independent_storage", high_result == 0 && *static_cast<volatile unsigned char*>(high) == 0xa7 && memory.Unmap(high, kPage) == 0);
    std::int64_t preserved = -1;
    check("exhausted_range_preserves_output_and_reports_capacity", memory.Allocate(0, startup_size + movie_size, kPage, kPage, 0, &preserved) == kAgain && preserved == -1);
    check("large_reserved_arenas_release_cleanly", memory.Release(static_cast<std::uint64_t>(movie), movie_size) == 0 && memory.Release(static_cast<std::uint64_t>(startup), startup_size) == 0 && memory.Empty());
    std::printf("{\"event\":\"memory_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
