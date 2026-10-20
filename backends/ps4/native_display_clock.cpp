#include "native_display_clock.h"
#include "native_equeue.h"
#include "native_time.h"
#include "native_diagnostics.h"

#include <windows.h>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
class Period {
public:
    Period(std::uint64_t frequency, std::uint32_t numerator, std::uint32_t denominator)
        : frequency_(frequency), numerator_(numerator), denominator_(denominator) {
        if (!frequency || !numerator || numerator > 1000000 || !denominator || denominator > 1000000 ||
            frequency > (std::numeric_limits<std::uint64_t>::max)() / denominator ||
            frequency * denominator < numerator) { throw std::runtime_error("Invalid display clock period"); }
    }
    std::uint64_t Deadline(std::uint64_t counter) const {
        return Narrow((static_cast<unsigned __int128>(counter) * frequency_ * denominator_ + numerator_ - 1) / numerator_);
    }
    std::uint64_t Count(std::uint64_t elapsed) const {
        return Narrow(static_cast<unsigned __int128>(elapsed) * numerator_ / (frequency_ * denominator_));
    }
    std::uint64_t Relative100ns(std::uint64_t ticks) const {
        return Narrow((static_cast<unsigned __int128>(ticks) * 10000000 + frequency_ - 1) / frequency_);
    }
private:
    std::uint64_t frequency_;
    std::uint32_t numerator_, denominator_;
    static std::uint64_t Narrow(unsigned __int128 value) {
        if (value > (std::numeric_limits<std::uint64_t>::max)()) { throw std::overflow_error("Display clock overflow"); }
        return static_cast<std::uint64_t>(value);
    }
};
std::uint64_t Qpc() {
    LARGE_INTEGER value{};
    if (!QueryPerformanceCounter(&value) || value.QuadPart < 0) { throw std::runtime_error("Display QPC failed"); }
    return static_cast<std::uint64_t>(value.QuadPart);
}
std::uint64_t Frequency() {
    LARGE_INTEGER value{};
    if (!QueryPerformanceFrequency(&value) || value.QuadPart <= 0) { throw std::runtime_error("Display QPC frequency failed"); }
    return static_cast<std::uint64_t>(value.QuadPart);
}
constexpr int kInvalidQueue = static_cast<int>(0x8029000cu), kInvalidHandle = static_cast<int>(0x8029000bu);
}

struct NativeDisplayClock::Impl {
    struct Subscription { std::uint64_t queue, ident; };
    const std::uint64_t owner;
    const Period period;
    const std::uint64_t start;
    mutable std::mutex mutex;
    std::mutex lifecycle;
    std::condition_variable changed;
    HANDLE stop{}, timer{};
    std::thread thread;
    bool closed{};
    unsigned divider = 1;
    std::uint64_t rate_revision{};
    unsigned waiters{};
    NativeVblankStatus status;
    std::vector<Subscription> subscriptions;

    Impl(std::uint64_t id, std::uint32_t numerator, std::uint32_t denominator)
        : owner(id), period(Frequency(), numerator, denominator), start(Qpc()) {}
    ~Impl() {
        try { Stop(); }
        catch (...) { UnsupportedApi("native_display_clock_shutdown", 0, nullptr); }
        if (timer) { CloseHandle(timer); }
        if (stop) { CloseHandle(stop); }
    }
    void Start() {
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer && GetLastError() == ERROR_INVALID_PARAMETER) {
            timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
        }
        if (!stop || !timer) { throw std::runtime_error("Could not create display clock wait handles"); }
        thread = std::thread([this] {
            try { Run(); }
            catch (const std::exception& error) {
                std::fprintf(stderr, "Display clock error: %s\n", error.what());
                UnsupportedApi("native_display_clock_worker", GetLastError(), nullptr);
            }
        });
    }
    void Stop() {
        std::lock_guard lifetime(lifecycle);
        { std::lock_guard lock(mutex); closed = true; }
        changed.notify_all();
        if (stop) { SetEvent(stop); }
        if (thread.joinable()) { thread.join(); }
        std::lock_guard lock(mutex);
        for (const auto& subscription : subscriptions) { (void)RemoveNativeVideoEvent(subscription.queue, subscription.ident, owner); }
        subscriptions.clear();
    }
    void Run() {
        std::uint64_t previous{};
        for (;;) {
            const auto elapsed = Qpc() - start;
            const auto deadline = period.Deadline(previous + 1);
            if (elapsed < deadline) {
                const auto relative = period.Relative100ns(deadline - elapsed);
                if (relative > static_cast<std::uint64_t>((std::numeric_limits<LONGLONG>::max)())) { throw std::overflow_error("Display wait overflow"); }
                LARGE_INTEGER due{};
                due.QuadPart = -static_cast<LONGLONG>((std::max)(relative, 1ull));
                if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) { throw std::runtime_error("Display timer arm failed"); }
                const HANDLE handles[]{stop, timer};
                const auto result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                if (result == WAIT_OBJECT_0) { return; }
                if (result != WAIT_OBJECT_0 + 1) { throw std::runtime_error("Display timer wait failed"); }
            }
            const auto count = period.Count(Qpc() - start);
            if (count <= previous) { continue; }
            std::lock_guard lock(mutex);
            if (closed) { return; }
            status = {count, NativeProcessMicroseconds(), NativeHostTsc(), 0, 0, {}};
            for (auto iterator = subscriptions.begin(); iterator != subscriptions.end();) {
                if (iterator->ident == 7 && !EmitNativeVideoEvent(iterator->queue, 7, owner, count, status.tsc, count - previous)) { iterator = subscriptions.erase(iterator); }
                else { ++iterator; }
            }
            previous = count;
            changed.notify_all();
        }
    }
};

NativeDisplayClock::NativeDisplayClock(std::uint64_t owner, std::uint32_t numerator, std::uint32_t denominator)
    : impl_(std::make_unique<Impl>(owner, numerator, denominator)) {
    impl_->Start();
    std::printf("{\"event\":\"native_display_clock_start\",\"source\":\"software_reference\",\"numerator\":%u,\"denominator\":%u,\"owner\":%llu}\n", numerator, denominator, owner);
}
NativeDisplayClock::~NativeDisplayClock() = default;
void NativeDisplayClock::Stop() { impl_->Stop(); }
bool NativeDisplayClock::SetFlipRate(int rate) {
    if (rate < 0 || rate > 2) { return false; }
    std::lock_guard lock(impl_->mutex);
    if (impl_->closed) { return false; }
    impl_->divider = static_cast<unsigned>(rate + 1);
    ++impl_->rate_revision;
    impl_->changed.notify_all();
    return true;
}
std::uint64_t NativeDisplayClock::NextFlipCounter() const {
    return ReserveFlipOpportunity().counter;
}
NativeDisplayClock::FlipOpportunity NativeDisplayClock::ReserveFlipOpportunity() const {
    std::lock_guard lock(impl_->mutex);
    return {(impl_->status.count / impl_->divider + 1) * impl_->divider, impl_->rate_revision};
}
bool NativeDisplayClock::WaitVblank() {
    std::unique_lock lock(impl_->mutex);
    const auto before = impl_->status.count;
    ++impl_->waiters;
    impl_->changed.wait(lock, [&] { return impl_->closed || impl_->status.count != before; });
    --impl_->waiters;
    return !impl_->closed;
}
bool NativeDisplayClock::WaitFlipOpportunity() {
    return WaitFlipOpportunity(ReserveFlipOpportunity());
}
bool NativeDisplayClock::WaitFlipOpportunity(FlipOpportunity opportunity) {
    std::unique_lock lock(impl_->mutex);
    while (!impl_->closed) {
        if (!opportunity.counter || opportunity.revision != impl_->rate_revision) {
            opportunity = {(impl_->status.count / impl_->divider + 1) * impl_->divider, impl_->rate_revision};
        }
        impl_->changed.wait(lock, [&] { return impl_->closed || impl_->status.count >= opportunity.counter || impl_->rate_revision != opportunity.revision; });
        if (impl_->closed) { return false; }
        if (impl_->rate_revision == opportunity.revision && impl_->status.count >= opportunity.counter) { return true; }
    }
    return false;
}
NativeVblankStatus NativeDisplayClock::Snapshot() const { std::lock_guard lock(impl_->mutex); return impl_->status; }
int NativeDisplayClock::Subscribe(std::uint64_t queue, bool flip, void* user_data) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->closed) { return kInvalidHandle; }
    const std::uint64_t ident = flip ? 6 : 7;
    const auto existing = std::find_if(impl_->subscriptions.begin(), impl_->subscriptions.end(), [&](const auto& subscription) { return subscription.queue == queue && subscription.ident == ident; });
    const bool duplicate = existing != impl_->subscriptions.end();
    if (!duplicate) {
        if (impl_->subscriptions.size() >= 1024) { return static_cast<int>(0x8029100cu); }
        impl_->subscriptions.reserve(impl_->subscriptions.size() + 1);
    }
    const auto result = AddNativeVideoEvent(queue, ident, impl_->owner, user_data);
    if (result) { return kInvalidQueue; }
    if (!duplicate) { impl_->subscriptions.push_back({queue, ident}); }
    return 0;
}
int NativeDisplayClock::Unsubscribe(std::uint64_t queue, bool flip) {
    std::lock_guard lock(impl_->mutex);
    const std::uint64_t ident = flip ? 6 : 7;
    const auto result = RemoveNativeVideoEvent(queue, ident, impl_->owner);
    if (result) { return kInvalidQueue; }
    std::erase_if(impl_->subscriptions, [&](const auto& item) { return item.queue == queue && item.ident == ident; });
    return 0;
}
void NativeDisplayClock::NotifyFlipCompleted(std::int64_t argument) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->closed) { return; }
    for (auto iterator = impl_->subscriptions.begin(); iterator != impl_->subscriptions.end();) {
        if (iterator->ident == 6 && !EmitNativeVideoEvent(iterator->queue, 6, impl_->owner, static_cast<std::uint64_t>(argument), NativeHostTsc())) { iterator = impl_->subscriptions.erase(iterator); }
        else { ++iterator; }
    }
}

bool DisplayClockSelfTest() {
    bool passed = true;
    auto check = [&](const char* name, bool result) {
        passed &= result;
        std::printf("{\"event\":\"display_clock_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    Period exact(10000000, 60, 1), ntsc(10000000, 60000, 1001);
    Period high_frequency(4000000000ull, 60, 1);
    check("integer_and_rational_periods", exact.Deadline(60) == 10000000 && ntsc.Deadline(60000) == 10010000000ull && high_frequency.Deadline(60) == 4000000000ull);
    check("deadline_rounding_never_advances_early", exact.Count(exact.Deadline(17) - 1) == 16 && exact.Count(exact.Deadline(17)) == 17);
    constexpr std::uint64_t year_frames = 60ull * 60 * 60 * 24 * 365;
    check("long_run_has_no_accumulated_truncation", exact.Deadline(year_frames) == 10000000ull * 60 * 60 * 24 * 365 && exact.Count(exact.Deadline(year_frames)) == year_frames);
    NativeDisplayClock clock(0x10000, 60, 1);
    const auto before = NativeProcessMicroseconds();
    const bool waited = clock.WaitVblank() && clock.WaitVblank();
    const auto status = clock.Snapshot();
    check("real_clock_wait_and_timestamp_domains", waited && status.count >= 2 && status.process_microseconds >= before && status.tsc && status.tsc != status.process_microseconds);
    const bool rate = clock.SetFlipRate(2) && clock.NextFlipCounter() % 3 == 0 && clock.WaitFlipOpportunity();
    check("flip_rate_changes_eligible_boundaries", rate && !clock.SetFlipRate(-1) && clock.SetFlipRate(0));
    const auto reserved = clock.ReserveFlipOpportunity();
    const bool reached = clock.WaitFlipOpportunity(reserved) && clock.Snapshot().count >= reserved.counter;
    check("reserved_boundary_can_complete_after_preparation_without_requesting_another", reached && clock.WaitFlipOpportunity(reserved));
    const auto old_rate = clock.ReserveFlipOpportunity();
    check("reserved_boundary_rechecks_a_changed_flip_rate", clock.SetFlipRate(2) && clock.WaitFlipOpportunity(old_rate) && clock.Snapshot().count >= old_rate.counter && clock.SetFlipRate(0));
    clock.Stop();
    check("stop_cancels_waits_and_is_idempotent", !clock.WaitVblank() && !clock.WaitFlipOpportunity() && !clock.SetFlipRate(0));
    clock.Stop();
    NativeDisplayClock slow(0x10001, 1, 1);
    bool cancelled = false;
    std::thread waiter([&] { cancelled = !slow.WaitVblank(); });
    bool waiting = false;
    const auto timeout = GetTickCount64() + 500;
    while (GetTickCount64() < timeout) {
        { std::lock_guard lock(slow.impl_->mutex); waiting = slow.impl_->waiters != 0; }
        if (waiting) { break; }
        Sleep(0);
    }
    slow.Stop();
    waiter.join();
    check("stop_wakes_already_blocked_waiter", waiting && cancelled && slow.Snapshot().count == 0);
    bool overflow = false;
    try { (void)exact.Deadline((std::numeric_limits<std::uint64_t>::max)()); }
    catch (const std::overflow_error&) { overflow = true; }
    check("period_overflow_is_checked", overflow);
    std::printf("{\"event\":\"display_clock_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
