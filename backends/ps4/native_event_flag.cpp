#include "native_event_flag.h"
#include "native_diagnostics.h"
#include "native_host_timing.h"
#include "native_threads.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#define KT_SYSV __attribute__((sysv_abi))
namespace {
using Handle = std::uint64_t;
constexpr int Error(unsigned code) { return static_cast<int>(0x80020000u | code); }
constexpr int kPermission = Error(1), kMissing = Error(3), kMemory = Error(12), kDeleted = Error(13), kBusy = Error(16);
constexpr int kValue = Error(22), kTimeout = Error(60), kName = Error(63), kCancelled = Error(85), kPending = 1;
struct Waiter {
    std::condition_variable ready;
    std::uint64_t bits{}, result_bits{};
    unsigned mode{};
    int priority{}, result = kPending;
};
struct Flag {
    std::mutex mutex;
    std::condition_variable changed;
    std::list<Waiter*> waiters;
    std::uint64_t bits{};
    bool multi{}, priority{}, deleted{};
};
struct Registry {
    std::mutex mutex;
    std::map<Handle, std::shared_ptr<Flag>> flags;
    Handle next = 0x4556460000000001ull;
} registry;
std::atomic<unsigned> wait_logs{}, set_logs{};
template<typename Function> __declspec(noinline) int Api(const void* caller, Function&& function) {
    try { return function(); }
    catch (const std::bad_alloc&) { return kMemory; }
    catch (...) { UnsupportedApi("native_event_flag_host", 0, caller); }
}
std::shared_ptr<Flag> Find(Handle id) {
    std::lock_guard lock(registry.mutex);
    const auto found = registry.flags.find(id); return found == registry.flags.end() ? nullptr : found->second;
}
bool Matches(const Flag& flag, const Waiter& waiter) {
    return (waiter.mode & 15) == 1 ? (flag.bits & waiter.bits) == waiter.bits : (flag.bits & waiter.bits) != 0;
}
void Consume(Flag& flag, const Waiter& waiter) {
    if ((waiter.mode & 0xf0) == 0x10) { flag.bits = 0; }
    else if ((waiter.mode & 0xf0) == 0x20) { flag.bits &= ~waiter.bits; }
}
void Grant(Flag& flag) {
    for (auto it = flag.waiters.begin(); it != flag.waiters.end();) {
        auto& waiter = **it;
        if (!Matches(flag, waiter)) { ++it; continue; }
        waiter.result_bits = flag.bits; Consume(flag, waiter); waiter.result = 0;
        it = flag.waiters.erase(it); waiter.ready.notify_one();
    }
}
int Create(Handle* output, const char* name, unsigned attributes, std::uint64_t initial, const void* options, const void* caller) {
    return Api(caller, [&] {
        if (!output || !name || options || (attributes & ~0x33u) || (attributes & 3) == 3 || (attributes & 0x30) == 0x30) { return kValue; }
        const auto bytes = strnlen(name, 32); if (bytes >= 32) { return kName; }
        auto flag = std::make_shared<Flag>(); flag->bits = initial; flag->multi = (attributes & 0x20) != 0; flag->priority = (attributes & 3) == 2;
        std::lock_guard lock(registry.mutex);
        if (registry.flags.size() >= 256 || registry.next == UINT64_MAX) { return kMemory; }
        const auto id = registry.next++; registry.flags.emplace(id, flag); *output = id;
        std::printf("{\"event\":\"native_event_flag_create\",\"handle\":%llu,\"attr\":%u,\"initial_bits\":%llu,\"name_bytes\":%zu}\n", id, attributes, initial, bytes);
        return 0;
    });
}
int Wait(Handle id, std::uint64_t bits, unsigned mode, std::uint64_t* output, unsigned* timeout, bool poll, const void* caller) {
    NativeHostOperationScope timing("sceKernelWaitEventFlag", "synchronization", bits, caller, !poll);
    return Api(caller, [&] {
        const auto flag = Find(id); if (!flag) { return kMissing; }
        if (!bits || (mode & ~0x33u) || ((mode & 3) != 1 && (mode & 3) != 2) || (mode & 0x30) == 0x30) { return kValue; }
        std::unique_lock lock(flag->mutex);
        if (flag->deleted) { return kMissing; }
        if (!flag->multi && !flag->waiters.empty()) { return kPermission; }
        Waiter waiter; waiter.bits = bits; waiter.mode = mode; waiter.priority = NativeGuestThreadPriority();
        if (Matches(*flag, waiter)) { if (output) { *output = flag->bits; } Consume(*flag, waiter); return 0; }
        if (poll || (timeout && !*timeout)) { if (output) { *output = flag->bits; } return poll ? kBusy : kTimeout; }
        auto insertion = flag->waiters.end();
        if (flag->priority) {
            insertion = flag->waiters.begin();
            while (insertion != flag->waiters.end() && (*insertion)->priority <= waiter.priority) { ++insertion; }
        }
        const auto queued = flag->waiters.insert(insertion, &waiter);
        flag->changed.notify_all();
        const auto start = std::chrono::steady_clock::now();
        const unsigned budget = timeout ? *timeout : 0;
        const auto sequence = wait_logs.fetch_add(1);
        if (sequence < 32) { std::printf("{\"event\":\"native_event_flag_wait_begin\",\"handle\":%llu,\"bits\":%llu,\"mode\":%u,\"timed\":%s}\n", id, bits, mode, timeout ? "true" : "false"); }
        try {
            if (timeout) {
                if (!waiter.ready.wait_until(lock, start + std::chrono::microseconds(budget), [&] { return waiter.result != kPending; })) {
                    flag->waiters.erase(queued); waiter.result_bits = flag->bits; waiter.result = kTimeout;
                }
            } else { waiter.ready.wait(lock, [&] { return waiter.result != kPending; }); }
        } catch (...) { UnsupportedApi("native_event_flag_wait_host_failure", id, caller); }
        if (output) { *output = waiter.result_bits; }
        if (timeout) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
            *timeout = waiter.result || elapsed >= budget ? 0 : budget - static_cast<unsigned>(elapsed);
        }
        if (sequence < 32) { std::printf("{\"event\":\"native_event_flag_wait_end\",\"handle\":%llu,\"result\":%d,\"result_bits\":%llu}\n", id, waiter.result, waiter.result_bits); }
        return waiter.result;
    });
}
int Set(Handle id, std::uint64_t bits, bool clear, const void* caller) {
    return Api(caller, [&] {
        const auto flag = Find(id); if (!flag) { return kMissing; }
        std::lock_guard lock(flag->mutex); if (flag->deleted) { return kMissing; }
        // ClearEventFlag takes a preservation mask, unlike CLEAR_PAT wait mode.
        if (clear) { flag->bits &= bits; } else { flag->bits |= bits; Grant(*flag); }
        if (set_logs.fetch_add(1) < 32) { std::printf("{\"event\":\"native_event_flag_update\",\"handle\":%llu,\"clear\":%s,\"bits\":%llu}\n", id, clear ? "true" : "false", flag->bits); }
        return 0;
    });
}
void Retire(Flag& flag, int result) {
    for (auto* waiter : flag.waiters) { waiter->result = result; waiter->result_bits = flag.bits; waiter->ready.notify_one(); }
    flag.waiters.clear();
}
int Cancel(Handle id, std::uint64_t pattern, int* waiting, const void* caller) {
    return Api(caller, [&] {
        const auto flag = Find(id); if (!flag) { return kMissing; }
        std::lock_guard lock(flag->mutex); if (flag->deleted) { return kMissing; }
        if (waiting) { *waiting = static_cast<int>(flag->waiters.size()); }
        flag->bits = pattern; Retire(*flag, kCancelled); return 0;
    });
}
int Delete(Handle id, const void* caller) {
    return Api(caller, [&] {
        std::lock_guard registry_lock(registry.mutex);
        const auto found = registry.flags.find(id); if (found == registry.flags.end()) { return kMissing; }
        const auto flag = found->second; std::lock_guard lock(flag->mutex);
        flag->deleted = true; Retire(*flag, kDeleted); registry.flags.erase(found); return 0;
    });
}
int KT_SYSV EventCreate(Handle* output, const char* name, unsigned attr, std::uint64_t initial, const void* options) { return Create(output, name, attr, initial, options, __builtin_return_address(0)); }
int KT_SYSV EventWait(Handle id, std::uint64_t bits, unsigned mode, std::uint64_t* output, unsigned* timeout) { return Wait(id, bits, mode, output, timeout, false, __builtin_return_address(0)); }
int KT_SYSV EventPoll(Handle id, std::uint64_t bits, unsigned mode, std::uint64_t* output) { return Wait(id, bits, mode, output, nullptr, true, __builtin_return_address(0)); }
int KT_SYSV EventSet(Handle id, std::uint64_t bits) { return Set(id, bits, false, __builtin_return_address(0)); }
int KT_SYSV EventClear(Handle id, std::uint64_t bits) { return Set(id, bits, true, __builtin_return_address(0)); }
int KT_SYSV EventCancel(Handle id, std::uint64_t bits, int* waiting) { return Cancel(id, bits, waiting, __builtin_return_address(0)); }
int KT_SYSV EventDelete(Handle id) { return Delete(id, __builtin_return_address(0)); }
}
std::uint64_t ResolveEventFlagImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libkernel" || module != "libkernel") { return 0; }
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "BpFoboUJoZU") { return reinterpret_cast<std::uint64_t>(&EventCreate); }
    if (id == "JTvBflhYazQ") { return reinterpret_cast<std::uint64_t>(&EventWait); }
    if (id == "9lvj5DjHZiA") { return reinterpret_cast<std::uint64_t>(&EventPoll); }
    if (id == "IOnSvHzqu6A") { return reinterpret_cast<std::uint64_t>(&EventSet); }
    if (id == "7uhBFWRAS60") { return reinterpret_cast<std::uint64_t>(&EventClear); }
    if (id == "PZku4ZrXJqg") { return reinterpret_cast<std::uint64_t>(&EventCancel); }
    if (id == "8mql9OcQnd4") { return reinterpret_cast<std::uint64_t>(&EventDelete); }
    return 0;
}
bool EventFlagSelfTest() {
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value; std::printf("{\"event\":\"event_flag_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    Handle id = 73;
    check("invalid_create_preserves_handle", Create(&id, "flag", 3, 0, nullptr, nullptr) == kValue && id == 73 && Create(nullptr, "flag", 0, 0, nullptr, nullptr) == kValue);
    const char too_long[] = "12345678901234567890123456789012";
    check("name_bound_is_31_bytes", Create(&id, too_long, 0, 0, nullptr, nullptr) == kName && id == 73);
    check("opaque_handle_and_64_bit_initial_pattern", Create(&id, "AuthoredFlag", 0x21, 0x100000002ull, nullptr, nullptr) == 0 && id > UINT32_MAX);
    std::uint64_t result{};
    check("and_wait_does_not_clear_without_clear_mode", Wait(id, 0x100000002ull, 1, &result, nullptr, true, nullptr) == 0 && result == 0x100000002ull);
    check("clear_pattern_preserves_unrequested_bits", Wait(id, 2, 0x22, &result, nullptr, true, nullptr) == 0 && result == 0x100000002ull &&
        Wait(id, 0x100000000ull, 1, &result, nullptr, true, nullptr) == 0 && result == 0x100000000ull);
    check("clear_all_consumes_the_entire_pattern", Wait(id, 0x100000000ull, 0x12, &result, nullptr, true, nullptr) == 0 &&
        Wait(id, 1, 2, &result, nullptr, true, nullptr) == kBusy && result == 0);
    check("set_or_and_clear_preservation_mask", Set(id, 7, false, nullptr) == 0 && Set(id, ~std::uint64_t{2}, true, nullptr) == 0 &&
        Wait(id, 5, 1, &result, nullptr, true, nullptr) == 0 && result == 5);
    unsigned timeout = 1000;
    check("finite_wait_returns_timeout_and_snapshot", Wait(id, 8, 2, &result, &timeout, false, nullptr) == kTimeout && timeout == 0 && result == 5);
    check("invalid_wait_never_consumes_pattern", Wait(id, 0, 2, &result, nullptr, true, nullptr) == kValue && Wait(id, 1, 0x32, &result, nullptr, true, nullptr) == kValue);
    std::atomic<int> waited{73};
    std::thread waiter([&] { waited = Wait(id, 8, 0x12, &result, nullptr, false, nullptr); });
    const auto queued = [&](std::size_t count) {
        const auto flag = Find(id); if (!flag) { return false; }
        std::unique_lock lock(flag->mutex);
        return flag->changed.wait_for(lock, std::chrono::seconds(2), [&] { return flag->waiters.size() == count; });
    };
    const bool waiting = queued(1); Set(id, 8, false, nullptr); waiter.join();
    check("cross_thread_set_wakes_real_waiter_and_clears_atomically", waiting && waited == 0 && result == 13);
    std::thread cancelled([&] { waited = Wait(id, 16, 1, &result, nullptr, false, nullptr); });
    const bool cancel_waiting = queued(1); int count{}; Cancel(id, 32, &count, nullptr); cancelled.join();
    check("cancel_wakes_waiters_and_installs_new_pattern", cancel_waiting && count == 1 && waited == kCancelled && result == 32);
    std::thread retired([&] { waited = Wait(id, 64, 1, &result, nullptr, false, nullptr); });
    const bool delete_waiting = queued(1); Delete(id, nullptr); retired.join();
    check("delete_wakes_waiters_without_reusing_identity", delete_waiting && waited == kDeleted && Delete(id, nullptr) == kMissing && Set(id, 1, false, nullptr) == kMissing);
    const auto old = id; Create(&id, "single", 0x10, 0, nullptr, nullptr);
    check("new_object_has_distinct_identity", old != id);
    std::thread single([&] { waited = Wait(id, 1, 2, nullptr, nullptr, false, nullptr); });
    const bool single_waiting = queued(1);
    check("single_attribute_rejects_second_waiter", single_waiting && Wait(id, 2, 2, nullptr, nullptr, true, nullptr) == kPermission);
    Set(id, 1, false, nullptr); single.join(); Delete(id, nullptr);
    check("kernel_namespace_is_exact", !ResolveEventFlagImport("BpFoboUJoZU", "libScePosix", "libkernel") && !ResolveEventFlagImport("BpFoboUJoZU", "libkernel", "wrong"));
    std::printf("{\"event\":\"event_flag_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
