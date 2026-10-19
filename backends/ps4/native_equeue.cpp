#include "native_equeue.h"
#include "native_diagnostics.h"
#include "native_host_timing.h"
#include "native_time.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
constexpr int Error(unsigned number) { return static_cast<int>(0x80020000u | number); }
constexpr int kBadHandle = Error(9), kInvalid = Error(22), kMemory = Error(12), kAddress = Error(14), kMissing = Error(2), kTimeout = Error(60);
struct Entry { NativeKernelEvent event{}; std::uint64_t owner{}; bool pending{}; };
struct Queue {
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::uint64_t, Entry> video_events;
    bool closed{};
    unsigned waiters{};
};
struct Registry {
    std::mutex mutex;
    std::map<std::uint64_t, std::shared_ptr<Queue>> queues;
    std::uint64_t next = 0x45510001;
};
Registry& Queues() {
    // Process-lifetime registry: producers must never encounter a destroyed
    // static mutex during CRT teardown. Individual queues still have explicit
    // Delete ownership and shared lifetime for blocked waiters.
    static auto* registry = new Registry;
    return *registry;
}
std::shared_ptr<Queue> Find(std::uint64_t handle) {
    auto& registry = Queues();
    std::lock_guard lock(registry.mutex);
    const auto iterator = registry.queues.find(handle);
    return iterator == registry.queues.end() ? nullptr : iterator->second;
}
template <typename Function>
int Api(const void* caller, Function function) {
    try { return function(); }
    catch (const std::bad_alloc&) { return kMemory; }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Native event queue error: %s\n", error.what());
        UnsupportedApi("native_equeue_host", 0, caller);
    }
}
int KT_SYSV Create(std::uint64_t* output, const char* name) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] {
        if (!output || !name) { return kInvalid; }
        if (strnlen_s(name, 33) >= 32) { return Error(63); }
        auto queue = std::make_shared<Queue>();
        auto& registry = Queues();
        std::lock_guard lock(registry.mutex);
        if (registry.queues.size() >= 4096 || registry.next == 0x7fffffffffffffffull) { return kMemory; }
        const auto handle = registry.next++;
        registry.queues.emplace(handle, std::move(queue));
        *output = handle;
        TraceNativeApi("sceKernelCreateEqueue", handle, caller);
        return 0;
    });
}
int KT_SYSV Delete(std::uint64_t handle) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] {
        auto& registry = Queues();
        std::shared_ptr<Queue> queue;
        {
            std::lock_guard lock(registry.mutex);
            const auto iterator = registry.queues.find(handle);
            if (iterator == registry.queues.end()) { return kBadHandle; }
            queue = iterator->second;
            registry.queues.erase(iterator);
        }
        { std::lock_guard lock(queue->mutex); queue->closed = true; queue->video_events.clear(); }
        queue->changed.notify_all();
        return 0;
    });
}
bool Pending(const Queue& queue) {
    return std::any_of(queue.video_events.begin(), queue.video_events.end(), [](const auto& item) { return item.second.pending; });
}
int KT_SYSV Wait(std::uint64_t handle, NativeKernelEvent* events, int capacity, int* output, const std::uint32_t* timeout) {
    const auto caller = __builtin_return_address(0);
    NativeHostOperationScope timing("sceKernelWaitEqueue", "synchronization", handle, caller);
    return Api(caller, [&] {
        if (!output) { return kAddress; }
        *output = 0;
        const auto queue = Find(handle);
        if (!queue) { return kBadHandle; }
        if (!events) { return kAddress; }
        if (capacity < 1 || capacity > 1024) { return kInvalid; }
        const bool infinite = !timeout;
        const auto duration = timeout ? *timeout : 0;
        std::unique_lock lock(queue->mutex);
        const auto ready = [&] { return queue->closed || Pending(*queue); };
        if (!ready()) {
            ++queue->waiters;
            if (infinite) { queue->changed.wait(lock, ready); }
            else if (duration) { queue->changed.wait_for(lock, std::chrono::microseconds(duration), ready); }
            --queue->waiters;
        }
        if (queue->closed) { return kBadHandle; }
        for (auto& [ident, entry] : queue->video_events) {
            (void)ident;
            if (!entry.pending || *output == capacity) { continue; }
            events[(*output)++] = entry.event;
            entry.pending = false;
            entry.event.data = 0;
        }
        return *output ? 0 : kTimeout;
    });
}
const NativeKernelEvent& Checked(const NativeKernelEvent* event, const void* caller) {
    if (!event) { UnsupportedApi("native_equeue_null_event", 0, caller); }
    return *event;
}
std::uint64_t KT_SYSV GetId(const NativeKernelEvent* event) { return Checked(event, __builtin_return_address(0)).ident; }
std::uint64_t KT_SYSV GetData(const NativeKernelEvent* event) { return Checked(event, __builtin_return_address(0)).data; }
int KT_SYSV GetFilter(const NativeKernelEvent* event) { return Checked(event, __builtin_return_address(0)).filter; }
void* KT_SYSV GetUserData(const NativeKernelEvent* event) { return Checked(event, __builtin_return_address(0)).user_data; }
} // namespace

int AddNativeVideoEvent(std::uint64_t handle, std::uint64_t ident, std::uint64_t owner, void* user_data) {
    const auto queue = Find(handle);
    if (!queue) { return kBadHandle; }
    std::lock_guard lock(queue->mutex);
    if (queue->closed) { return kBadHandle; }
    auto iterator = queue->video_events.find(ident);
    if (iterator != queue->video_events.end()) {
        if (iterator->second.owner != owner) { return kInvalid; }
        iterator->second.event.user_data = user_data;
        return 0;
    }
    if (queue->video_events.size() >= 1024) { return kMemory; }
    queue->video_events.emplace(ident, Entry{{ident, -13, 0x20, 0, 0, user_data}, owner, false});
    return 0;
}
int RemoveNativeVideoEvent(std::uint64_t handle, std::uint64_t ident, std::uint64_t owner) {
    const auto queue = Find(handle);
    if (!queue) { return kBadHandle; }
    std::lock_guard lock(queue->mutex);
    if (queue->closed) { return kBadHandle; }
    const auto iterator = queue->video_events.find(ident);
    if (iterator == queue->video_events.end() || iterator->second.owner != owner) { return kMissing; }
    queue->video_events.erase(iterator);
    return 0;
}
bool EmitNativeVideoEvent(std::uint64_t handle, std::uint64_t ident, std::uint64_t owner,
                          std::uint64_t value, std::uint64_t tsc, std::uint64_t occurrences) {
    const auto queue = Find(handle);
    if (!queue) { return false; }
    std::lock_guard lock(queue->mutex);
    const auto iterator = queue->video_events.find(ident);
    if (queue->closed || iterator == queue->video_events.end() || iterator->second.owner != owner) { return false; }
    auto& entry = iterator->second;
    const auto previous = entry.pending ? ((entry.event.data >> 12) & 15) : 0;
    const auto count = (std::min)(15ull, previous + (std::min)(15ull, occurrences));
    entry.event.data = ((value & 0xffffffffffffull) << 16) | (count << 12) | (tsc & 0xfff);
    entry.pending = true;
    queue->changed.notify_all();
    return true;
}

std::uint64_t ResolveEqueueImport(std::string_view identifier) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "D0OdFMjp46I") { return reinterpret_cast<std::uint64_t>(&Create); }
    if (hash == "jpFjmgAC5AE") { return reinterpret_cast<std::uint64_t>(&Delete); }
    if (hash == "fzyMKs9kim0") { return reinterpret_cast<std::uint64_t>(&Wait); }
    if (hash == "mJ7aghmgvfc") { return reinterpret_cast<std::uint64_t>(&GetId); }
    if (hash == "23CPPI1tyBY") { return reinterpret_cast<std::uint64_t>(&GetFilter); }
    if (hash == "kwGyyjohI50") { return reinterpret_cast<std::uint64_t>(&GetData); }
    if (hash == "vz+pg2zdopI") { return reinterpret_cast<std::uint64_t>(&GetUserData); }
    return 0;
}

bool EqueueSelfTest() {
    bool passed = true;
    auto check = [&](const char* name, bool result) {
        passed &= result;
        std::printf("{\"event\":\"equeue_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    std::uint64_t handle = 99;
    check("parameters_and_output_preservation", Create(nullptr, "test") == kInvalid && Create(&handle, nullptr) == kInvalid && handle == 99);
    if (Create(&handle, "native event queue")) { return false; }
    NativeKernelEvent event{99, 99, 99, 99, 99, nullptr};
    int count = -1;
    const std::uint32_t poll = 0, short_wait = 1000;
    const bool polled = Wait(handle, &event, 1, &count, &poll) == kTimeout && count == 0 && event.ident == 99;
    const auto started = NativeProcessMicroseconds();
    const auto waited_timeout = Wait(handle, &event, 1, &count, &short_wait);
    check("poll_and_timeout_do_not_write_events", polled && waited_timeout == kTimeout && event.ident == 99 && NativeProcessMicroseconds() - started >= short_wait);
    check("subscription_owner_and_missing_handle", AddNativeVideoEvent(handle, 7, 1, reinterpret_cast<void*>(0x1234)) == 0 &&
        AddNativeVideoEvent(handle, 7, 2, nullptr) == kInvalid && AddNativeVideoEvent(0, 7, 1, nullptr) == kBadHandle);
    EmitNativeVideoEvent(handle, 7, 1, 40, 0xabc, 12);
    EmitNativeVideoEvent(handle, 7, 1, 50, 0xdef, 12);
    const bool delivered = Wait(handle, &event, 1, &count, &poll) == 0;
    check("display_event_packing_coalescing_and_clear", delivered && count == 1 && GetId(&event) == 7 && GetFilter(&event) == -13 &&
        GetUserData(&event) == reinterpret_cast<void*>(0x1234) && GetData(&event) == ((50ull << 16) | (15ull << 12) | 0xdef) && Wait(handle, &event, 1, &count, &poll) == kTimeout);
    check("event_removal", RemoveNativeVideoEvent(handle, 7, 1) == 0 && !EmitNativeVideoEvent(handle, 7, 1, 51, 0) && RemoveNativeVideoEvent(handle, 7, 1) == kMissing);
    const auto queue = Find(handle);
    int waited = 0;
    std::thread waiter([&] { NativeKernelEvent output{}; int available = -1; waited = Wait(handle, &output, 1, &available, nullptr); });
    bool waiting = false;
    const auto deadline = GetTickCount64() + 1000;
    while (GetTickCount64() < deadline) {
        { std::lock_guard lock(queue->mutex); waiting = queue->waiters != 0; }
        if (waiting) { break; }
        Sleep(0);
    }
    const auto deleted = Delete(handle);
    waiter.join();
    check("deleting_queue_wakes_live_waiter", waiting && deleted == 0 && waited == kBadHandle && Delete(handle) == kBadHandle);
    std::printf("{\"event\":\"equeue_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
