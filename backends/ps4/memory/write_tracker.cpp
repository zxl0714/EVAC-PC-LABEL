#include "write_tracker.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <thread>
#include <chrono>

namespace kt::memory {
// Separate translation unit: Clang does not model an asynchronous exception
// raised by an instruction in the same frame as the catching __try.
void AuthoredWriteTrackerFaultStore(unsigned char* address);
namespace {
constexpr std::uint64_t page = 4096;
constexpr std::size_t max_views = 4096, max_ranges = 256;
std::atomic<void*> active_tracker{};
std::atomic<unsigned> active_handlers{};
std::atomic<std::uint64_t> next_identity{1};
// Only the authored concurrency self-test installs these atomic spin barriers.
// Normal runtime leaves both null; the handler never invokes a callback.
std::atomic<std::atomic<bool>*> test_restore_entered{}, test_restore_release{};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free && std::atomic<void*>::is_always_lock_free);
bool Overlaps(std::uint64_t a, std::uint64_t n, std::uint64_t b, std::uint64_t m) { return a < b + m && b < a + n; }
bool Protection(unsigned value) { return value == PAGE_READWRITE || value == PAGE_READONLY || value == PAGE_NOACCESS; }
LONG CALLBACK FaultHandler(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2 && info->ExceptionRecord->ExceptionInformation[0] == 1 &&
        WriteTracker::HandleWriteFault(reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]))) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
__declspec(noinline) bool TryAuthoredWrite(unsigned char* address) {
    __try { AuthoredWriteTrackerFaultStore(address); return true; }
    __except(GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
}
}
struct WatchedView {
    std::uintptr_t address{};
    std::uint64_t bytes{}, physical{}, allocation{}, owner{};
    std::size_t slot{};
    std::atomic<unsigned> protection{};
    std::atomic<bool> active{true}, ever_armed{};
};
struct WatchedRange {
    std::uint64_t allocation{}, physical{}, bytes{}, guard_start{}, guard_bytes{}, last_use{};
    std::atomic<std::uint64_t> generation{1};
    std::atomic<bool> active{true}, armed{};
};
struct WatchedHostWrite {
    struct Segment { std::shared_ptr<WatchedView> view; std::uint64_t physical{}, bytes{}; };
    std::vector<Segment> segments;
};
bool WriteStamp::Unchanged() const noexcept {
    return range_ && range_->active.load() && range_->armed.load() && range_->generation.load() == generation_;
}
struct WriteTracker::Impl {
    std::mutex mutex;
    const std::uint64_t identity = next_identity.fetch_add(1);
    std::size_t view_limit{}, range_limit{};
    std::array<std::shared_ptr<WatchedView>, max_views> views;
    std::array<std::shared_ptr<WatchedRange>, max_ranges> ranges;
    std::array<std::atomic<WatchedView*>, max_views> published_views{};
    std::array<std::atomic<WatchedRange*>, max_ranges> published_ranges{};
    std::vector<std::shared_ptr<WatchedView>> retired_views;
    std::vector<std::shared_ptr<WatchedRange>> retired_ranges;
    std::vector<std::shared_ptr<WatchedHostWrite>> host_writes;
    std::atomic<bool> enabled{true};
    std::atomic<std::uint64_t> faults{}, arms{}, protected_bytes{};
    std::uint64_t uses{};
    void* handler{};

    bool Writable(std::uintptr_t address, std::uint64_t bytes, WatchedHostWrite* output = nullptr) {
        if (!bytes) { return true; }
        if (!address || bytes > UINTPTR_MAX - address) { return false; }
        const auto end = address + bytes;
        for (auto cursor = address; cursor < end;) {
            std::shared_ptr<WatchedView> owned;
            auto next_view = end;
            for (const auto& view : views) {
                if (!view || !view->active.load()) { continue; }
                if (cursor >= view->address && cursor - view->address < view->bytes) { owned = view; break; }
                if (view->address > cursor) { next_view = std::min(next_view, view->address); }
            }
            if (owned) {
                if (owned->protection.load() != PAGE_READWRITE) { return false; }
                const auto next = std::min(end, owned->address + owned->bytes);
                if (output) {
                    const auto begin = (owned->physical + cursor - owned->address) & ~(page - 1);
                    const auto limit = (owned->physical + next - owned->address + page - 1) & ~(page - 1);
                    output->segments.push_back({owned, begin, limit - begin});
                }
                cursor = next; continue;
            }
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT ||
                (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) { return false; }
            const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            if (info.RegionSize > UINTPTR_MAX - base || base + info.RegionSize <= cursor) { return false; }
            cursor = std::min(next_view, base + info.RegionSize);
        }
        return true;
    }

    // Pointer publication, the handler reader counter, and reclamation use
    // seq_cst ordering. A handler increments before loading any published raw
    // pointer; retired objects are freed only outside handlers with count zero.
    void Collect() { if (!active_handlers.load()) { retired_views.clear(); retired_ranges.clear(); } }
    void Invalidate(std::uint64_t allocation, std::uint64_t physical, std::uint64_t bytes) noexcept {
        for (std::size_t i = 0; i < range_limit; ++i) {
            auto* range = published_ranges[i].load();
            if (range && range->active.load() && range->allocation == allocation &&
                Overlaps(physical, bytes, range->guard_start, range->guard_bytes)) {
                range->generation.fetch_add(1); range->armed.store(false);
            }
        }
    }
    bool Restore(WatchedView& view, std::uint64_t physical, std::uint64_t bytes, bool from_fault = false) noexcept {
        const auto begin = std::max(view.physical, physical), end = std::min(view.physical + view.bytes, physical + bytes);
        if (begin >= end) { return true; }
        // Restoring one range can unprotect part of another watched range.
        // Invalidate EVERY physically overlapping range, not only the writer.
        Invalidate(view.allocation, begin, end - begin);
        if (from_fault) {
            if (auto* entered = test_restore_entered.load()) {
                auto* release = test_restore_release.load();
                entered->store(true);
                while (release && !release->load()) { YieldProcessor(); }
            }
        }
        DWORD previous{};
        const bool restored = VirtualProtect(reinterpret_cast<void*>(view.address + begin - view.physical),
            static_cast<SIZE_T>(end - begin), PAGE_READWRITE, &previous) != 0;
        // A normal observer can re-arm after the first invalidation but before
        // this protection call finishes. Invalidate again after the page is
        // writable, or that observer could retain a proof for unguarded bytes.
        Invalidate(view.allocation, begin, end - begin);
        if (restored) {
            // A guest protection change can finish while a fault restoration
            // is in flight. Do not leave RW pages behind a newer RO/NOACCESS
            // contract. Recheck after applying the latest published contract.
            for (;;) {
                const auto protection = view.protection.load();
                if (protection == PAGE_READWRITE) { break; }
                if (!VirtualProtect(reinterpret_cast<void*>(view.address + begin - view.physical),
                    static_cast<SIZE_T>(end - begin), protection, &previous)) { return false; }
                if (view.protection.load() == protection) { break; }
            }
        }
        return restored;
    }
    bool Fault(std::uintptr_t address) noexcept {
        for (std::size_t i = 0; i < view_limit; ++i) {
            auto* view = published_views[i].load();
            if (!view || !view->active.load() || address < view->address || address - view->address >= view->bytes ||
                view->protection.load() != PAGE_READWRITE || !view->ever_armed.load()) { continue; }
            const auto physical = view->physical + address - view->address;
            for (std::size_t j = 0; j < range_limit; ++j) {
                auto* range = published_ranges[j].load();
                if (range && range->active.load() && range->allocation == view->allocation &&
                    physical >= range->guard_start && physical - range->guard_start < range->guard_bytes) {
                    if (!Restore(*view, range->guard_start, range->guard_bytes, true)) { return false; }
                    if (view->protection.load() != PAGE_READWRITE) { return false; }
                    faults.fetch_add(1); return true;
                }
            }
            // A fault may have been queued just before normal-path retirement
            // restored the page. Retry only an owned, now-writable page.
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) &&
                info.State == MEM_COMMIT && info.Protect == PAGE_READWRITE) {
                Invalidate(view->allocation, physical & ~(page - 1), page);
                faults.fetch_add(1); return true;
            }
            return false;
        }
        return false;
    }
    void DisableLocked() {
        enabled.store(false);
        for (auto& range : ranges) { if (range) { range->generation.fetch_add(1); range->armed.store(false); } }
        for (auto& view : views) {
            if (view && view->active.load() && view->ever_armed.load() && view->protection.load() == PAGE_READWRITE) {
                if (!Restore(*view, view->physical, view->bytes)) { throw std::runtime_error("Cannot retire a tracked view's page protections"); }
            }
        }
        for (auto& range : ranges) { if (range) { range->active.store(false); } }
    }
    bool ValidateView(std::uintptr_t address, std::uint64_t bytes, unsigned protection) {
        for (auto cursor = address; cursor < address + bytes;) {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT ||
                info.Type != MEM_MAPPED || info.Protect != protection) { return false; }
            const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            if (end <= cursor) { return false; } cursor = std::min(address + bytes, end);
        }
        return true;
    }
};

WriteTracker::WriteTracker(std::size_t views, std::size_t ranges) : impl_(std::make_shared<Impl>()) {
    if (!views || views > max_views || !ranges || ranges > max_ranges) { throw std::invalid_argument("Invalid write-tracker bounds"); }
    impl_->view_limit = views; impl_->range_limit = ranges;
    void* expected{};
    if (!active_tracker.compare_exchange_strong(expected, impl_.get())) { throw std::runtime_error("Only one process write tracker may be active"); }
    impl_->handler = AddVectoredExceptionHandler(1, FaultHandler);
    if (!impl_->handler) { active_tracker.store(nullptr); throw std::runtime_error("Cannot install mapped-memory write tracker"); }
}
WriteTracker::~WriteTracker() {
    try { Disable(); }
    catch (...) { std::fputs("Write-tracker protection retirement failed\n", stderr); TerminateProcess(GetCurrentProcess(), 22); }
    active_tracker.store(nullptr);
    RemoveVectoredExceptionHandler(impl_->handler);
    while (active_handlers.load()) { SwitchToThread(); }
    for (auto& view : impl_->views) { if (view) { view->active.store(false); } }
}
WriteTracker::View WriteTracker::AddView(void* raw, std::uint64_t bytes, std::uint64_t physical, std::uint64_t allocation, unsigned protection) {
    auto& state = *impl_; std::lock_guard lock(state.mutex); state.Collect();
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    if (!state.enabled.load()) { return {}; }
    const auto fail = [&]() -> View { state.DisableLocked(); return {}; };
    if (!address || !bytes || !allocation || address % page || bytes % page || physical % page ||
        bytes > UINTPTR_MAX - address || bytes > UINT64_MAX - physical || !Protection(protection) ||
        !state.ValidateView(address, bytes, protection)) { return fail(); }
    for (const auto& view : state.views) {
        if (view && view->active.load() && Overlaps(address, bytes, view->address, view->bytes)) { return fail(); }
    }
    for (std::size_t i = 0; i < state.view_limit; ++i) {
        if (state.views[i]) { continue; }
        auto view = std::make_shared<WatchedView>();
        view->address = address; view->bytes = bytes; view->physical = physical; view->allocation = allocation;
        view->protection.store(protection); view->slot = i; view->owner = state.identity;
        state.views[i] = view; state.published_views[i].store(view.get());
        // Publish first, then invalidate: an observer cannot arm while missing
        // a newly returned writable alias and keep an old valid generation.
        state.Invalidate(allocation, physical, bytes); return view;
    }
    return fail();
}
void WriteTracker::RemoveView(const View& view) {
    auto& state = *impl_; std::lock_guard lock(state.mutex);
    if (!view || view->owner != state.identity || !view->active.load()) { return; }
    state.Invalidate(view->allocation, view->physical, view->bytes);
    if (view->ever_armed.load() && view->protection.load() == PAGE_READWRITE && !state.Restore(*view, view->physical, view->bytes)) {
        throw std::runtime_error("Cannot restore a removed tracked view");
    }
    view->active.store(false); state.published_views[view->slot].store(nullptr);
    state.retired_views.push_back(view); state.views[view->slot].reset(); state.Collect();
}
bool WriteTracker::ProtectView(const View& view, unsigned protection) {
    auto& state = *impl_; std::lock_guard lock(state.mutex);
    if (!view || view->owner != state.identity || !view->active.load() || !Protection(protection)) { return false; }
    state.Invalidate(view->allocation, view->physical, view->bytes);
    const auto old = view->protection.exchange(protection); DWORD previous{};
    if (VirtualProtect(reinterpret_cast<void*>(view->address), static_cast<SIZE_T>(view->bytes), protection, &previous)) { return true; }
    view->protection.store(old); state.Invalidate(view->allocation, view->physical, view->bytes); return false;
}
bool WriteTracker::Writable(const void* address, std::uint64_t bytes) const {
    std::lock_guard lock(impl_->mutex);
    return impl_->Writable(reinterpret_cast<std::uintptr_t>(address), bytes);
}
WriteTracker::HostWrite::~HostWrite() { Reset(); }
WriteTracker::HostWrite::HostWrite(HostWrite&&) noexcept = default;
WriteTracker::HostWrite& WriteTracker::HostWrite::operator=(HostWrite&& other) noexcept {
    if (this != &other) { Reset(); owner_ = std::move(other.owner_); write_ = std::move(other.write_); }
    return *this;
}
void WriteTracker::HostWrite::Reset() noexcept {
    if (owner_ && write_) {
        std::lock_guard lock(owner_->mutex);
        for (const auto& segment : write_->segments) { owner_->Invalidate(segment.view->allocation, segment.physical, segment.bytes); }
        std::erase(owner_->host_writes, write_);
    }
    write_.reset(); owner_.reset();
}
WriteTracker::HostWrite WriteTracker::BeginHostWrite(void* address, std::uint64_t bytes) {
    auto& state = *impl_; std::unique_lock lock(state.mutex);
    auto write = std::make_shared<WatchedHostWrite>();
    if (!state.Writable(reinterpret_cast<std::uintptr_t>(address), bytes, write.get())) { return {}; }
    HostWrite result; result.owner_ = impl_;
    if (write->segments.empty() || !state.enabled.load()) { return result; }
    // In-flight host writes cannot be dropped to fit a capacity bound. Disable
    // optional tracking instead; all later observers use content comparison.
    if (state.host_writes.size() >= max_views) { state.DisableLocked(); return result; }
    state.host_writes.push_back(write);
    result.write_ = std::move(write);
    try {
        for (const auto& segment : result.write_->segments) {
            if (segment.view->ever_armed.load() && !state.Restore(*segment.view, segment.physical, segment.bytes)) {
                state.DisableLocked();
                throw std::runtime_error("Cannot prepare a tracked host-write destination");
            }
        }
    } catch (...) { lock.unlock(); throw; }
    return result;
}
WriteStamp WriteTracker::Track(std::uint64_t allocation, std::uint64_t physical, std::uint64_t bytes) {
    auto& state = *impl_; std::lock_guard lock(state.mutex); state.Collect();
    if (!state.enabled.load() || !allocation || !bytes || bytes > 64 * 1024 * 1024 || physical > UINT64_MAX - bytes - (page - 1)) { return {}; }
    const auto guard_start = physical & ~(page - 1);
    const auto guard_bytes = ((physical + bytes + page - 1) & ~(page - 1)) - guard_start;
    for (const auto& write : state.host_writes) {
        for (const auto& segment : write->segments) {
            if (segment.view->allocation == allocation && Overlaps(guard_start, guard_bytes, segment.physical, segment.bytes)) { return {}; }
        }
    }
    bool readable{};
    for (const auto& view : state.views) {
        if (view && view->active.load() && view->allocation == allocation && physical >= view->physical &&
            physical - view->physical < view->bytes && bytes <= view->bytes - (physical - view->physical) &&
            view->protection.load() != PAGE_NOACCESS) { readable = true; break; }
    }
    if (!readable) { return {}; }
    std::shared_ptr<WatchedRange> selected;
    std::size_t slot = state.range_limit;
    for (std::size_t i = 0; i < state.range_limit; ++i) {
        const auto& range = state.ranges[i];
        if (range && range->active.load() && range->allocation == allocation && range->physical == physical && range->bytes == bytes) { selected = range; break; }
        if (!range && slot == state.range_limit) { slot = i; }
    }
    if (!selected) {
        if (slot == state.range_limit) {
            for (std::size_t i = 0; i < state.range_limit; ++i) {
                if (state.ranges[i].use_count() == 1 && (slot == state.range_limit || state.ranges[i]->last_use < state.ranges[slot]->last_use)) { slot = i; }
            }
            if (slot == state.range_limit) { return {}; }
            auto old = state.ranges[slot];
            for (const auto& view : state.views) {
                if (view && view->active.load() && view->allocation == old->allocation && view->protection.load() == PAGE_READWRITE &&
                    Overlaps(view->physical, view->bytes, old->guard_start, old->guard_bytes) && !state.Restore(*view, old->guard_start, old->guard_bytes)) {
                    state.DisableLocked(); return {};
                }
            }
            old->active.store(false); old->armed.store(false); state.published_ranges[slot].store(nullptr);
            state.retired_ranges.push_back(std::move(old));
        }
        selected = std::make_shared<WatchedRange>(); selected->allocation = allocation; selected->physical = physical; selected->bytes = bytes;
        selected->guard_start = physical & ~(page - 1);
        selected->guard_bytes = ((physical + bytes + page - 1) & ~(page - 1)) - selected->guard_start;
        state.ranges[slot] = selected; state.published_ranges[slot].store(selected.get());
    }
    selected->last_use = ++state.uses;
    if (!selected->armed.load()) {
        // Set before protection calls. A concurrent fault clears this flag;
        // never overwrite that notification after arming the last alias.
        // A stamp obtained during a racing write must never become valid
        // again merely because a later observer re-arms the same range.
        selected->generation.fetch_add(1); selected->armed.store(true);
        for (const auto& view : state.views) {
            if (!view || !view->active.load() || view->allocation != allocation || view->protection.load() != PAGE_READWRITE ||
                !Overlaps(view->physical, view->bytes, selected->guard_start, selected->guard_bytes)) { continue; }
            const auto begin = std::max(view->physical, selected->guard_start);
            const auto end = std::min(view->physical + view->bytes, selected->guard_start + selected->guard_bytes);
            view->ever_armed.store(true); DWORD previous{};
            if (!VirtualProtect(reinterpret_cast<void*>(view->address + begin - view->physical), static_cast<SIZE_T>(end - begin), PAGE_READONLY, &previous)) {
                state.DisableLocked(); return {};
            }
            state.protected_bytes.fetch_add(end - begin);
        }
        state.arms.fetch_add(1);
    }
    WriteStamp result; result.range_ = selected; result.generation_ = selected->generation.load(); return result;
}
void WriteTracker::Disable() { std::lock_guard lock(impl_->mutex); impl_->DisableLocked(); }
WriteTrackerStats WriteTracker::Stats() const noexcept { return {impl_->faults.load(), impl_->arms.load(), impl_->protected_bytes.load(), impl_->enabled.load()}; }
bool WriteTracker::HandleWriteFault(void* address) noexcept {
    active_handlers.fetch_add(1);
    auto* state = static_cast<Impl*>(active_tracker.load());
    const bool result = state && state->Fault(reinterpret_cast<std::uintptr_t>(address));
    active_handlers.fetch_sub(1); return result;
}
bool WriteTrackerPublicationRaceSelfTest(WriteTracker& tracker, unsigned char* address, std::uint64_t allocation, std::uint64_t physical) {
    const auto original = *static_cast<volatile unsigned char*>(address);
    const auto before = tracker.Track(allocation, physical, 4096);
    if (!before.Unchanged()) { return false; }
    std::atomic<bool> entered{}, release{};
    test_restore_release.store(&release); test_restore_entered.store(&entered);
    std::thread writer([&] { *static_cast<volatile unsigned char*>(address) = original ^ 0xff; });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!entered.load() && std::chrono::steady_clock::now() < deadline) { SwitchToThread(); }
    bool captured_valid{}; unsigned char captured{}; WriteStamp during;
    if (entered.load()) {
        during = tracker.Track(allocation, physical, 4096);
        captured = *static_cast<volatile unsigned char*>(address); captured_valid = during.Unchanged();
    }
    release.store(true); writer.join();
    test_restore_entered.store(nullptr); test_restore_release.store(nullptr);
    return entered.load() && captured_valid && captured == original && !during.Unchanged() &&
        *static_cast<volatile unsigned char*>(address) == (original ^ 0xff);
}
bool WriteTrackerProtectionRaceSelfTest(WriteTracker& tracker, const WriteTracker::View& view, unsigned char* address) {
    const auto proof = tracker.Track(1, 0, 4096);
    if (!proof.Unchanged()) { return false; }
    std::atomic<bool> entered{}, release{}; bool write_succeeded{};
    test_restore_release.store(&release); test_restore_entered.store(&entered);
    std::thread writer([&] { write_succeeded = TryAuthoredWrite(address); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!entered.load() && std::chrono::steady_clock::now() < deadline) { SwitchToThread(); }
    const bool protected_now = entered.load() && tracker.ProtectView(view, PAGE_NOACCESS);
    release.store(true); writer.join();
    test_restore_entered.store(nullptr); test_restore_release.store(nullptr);
    MEMORY_BASIC_INFORMATION info{};
    const bool correct = protected_now && !write_succeeded && VirtualQuery(address, &info, sizeof(info)) && info.Protect == PAGE_NOACCESS;
    const bool restored = tracker.ProtectView(view, PAGE_READWRITE);
    return correct && restored;
}
}
