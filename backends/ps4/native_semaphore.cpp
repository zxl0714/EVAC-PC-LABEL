#include "native_semaphore.h"
#include "native_diagnostics.h"
#include "native_host_timing.h"
#include "native_threads.h"
#include "diagnostics/monotonic_clock.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#define KT_SYSV __attribute__((sysv_abi))
namespace {
using Handle = std::uint64_t;
// The target reads qwords at object +0x38/+0x40. Its public semaphore ABI is
// pointer-sized, unlike the 32-bit SlotId in the current reference emulator.
static_assert(sizeof(Handle) == sizeof(void*) && sizeof(int) == 4);
constexpr int Error(unsigned code) { return static_cast<int>(0x80020000u | code); }
constexpr int kSearch = Error(3), kMemory = Error(12), kDeleted = Error(13), kAddress = Error(14), kBusy = Error(16);
constexpr int kInvalid = Error(22), kAgain = Error(35), kTimeout = Error(60), kName = Error(63), kCancelled = Error(85);
constexpr int kPending = 1;
using WaitPrimitive = NativeSemaphoreWaitPrimitive;
std::atomic<WaitPrimitive> wait_primitive{WaitPrimitive::Address}; // Other transports remain diagnostic controls.
struct Waiter {
    std::condition_variable ready;
    int requested, priority;
    std::atomic<int> result{kPending};
    NativeSemaphoreWaitTiming timing{};
    std::atomic<std::uint64_t> wake_began{}, wake_finished{};
    WaitPrimitive primitive{};
    HANDLE event{};
    std::shared_ptr<Waiter> notify_next;
    std::atomic<bool> notification_finished{true};
    Waiter(int count, int rank) : requested(count), priority(rank), primitive(wait_primitive.load()) {
        if (primitive == WaitPrimitive::Event) {
            event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!event) { throw std::bad_alloc(); }
        }
    }
    ~Waiter() { if (event) { CloseHandle(event); } }
    void Wake() noexcept {
        if (primitive == WaitPrimitive::Event) {
            if (!SetEvent(event)) { UnsupportedApi("native_semaphore_event_signal_failure", GetLastError(), nullptr); }
        } else if (primitive == WaitPrimitive::Address) { WakeByAddressSingle(&result); }
        else { ready.notify_one(); }
    }
};
static_assert(sizeof(std::atomic<int>) == 4 && std::atomic<int>::is_always_lock_free);
// The lease survives unlocking the semaphore and even the waiting thread's
// return. A raw pointer to its stack condition_variable would not be safe here.
// Linking already-owned nodes cannot allocate after credits are committed.
struct Notifications {
    std::shared_ptr<Waiter> first, last;
    void Add(const std::shared_ptr<Waiter>& waiter) noexcept {
        waiter->notification_finished.store(false, std::memory_order_relaxed);
        if (last) { last->notify_next = waiter; } else { first = waiter; }
        last = waiter;
    }
    void NotifyAll() noexcept {
        last.reset();
        while (first) {
            auto waiter = std::move(first); first = std::move(waiter->notify_next);
            waiter->wake_began.store(kt::diagnostics::MonotonicMicroseconds(), std::memory_order_release);
            waiter->Wake();
            waiter->wake_finished.store(kt::diagnostics::MonotonicMicroseconds(), std::memory_order_release);
            waiter->notification_finished.store(true, std::memory_order_release);
        }
    }
    ~Notifications() { NotifyAll(); }
};
std::shared_ptr<Waiter> AcquireWaiter(int count, int priority) {
    thread_local std::shared_ptr<Waiter> reusable;
    // A previous notifier may still own this CV after its waiter has resumed.
    // Never reuse it until that lease has also gone away.
    // use_count protects exclusive lifetime, not publication of node writes.
    // Pair with the notifier's release before reusing its linked-list node.
    if (!reusable || reusable.use_count() != 1 || !reusable->notification_finished.load(std::memory_order_acquire) ||
        reusable->primitive != wait_primitive.load()) {
        reusable = std::make_shared<Waiter>(count, priority);
    }
    else {
        reusable->requested = count; reusable->priority = priority;
        reusable->result = kPending; reusable->timing = {};
        reusable->wake_began = 0; reusable->wake_finished = 0;
        // Success may have been observed before parking, leaving the previous
        // auto-reset event signaled. Only exclusive, finished-notifier reuse
        // permits resetting it; never reset a live notification.
        if (reusable->event && !ResetEvent(reusable->event)) {
            UnsupportedApi("native_semaphore_event_reset_failure", GetLastError(), nullptr);
        }
    }
    return reusable;
}
struct Semaphore {
    std::mutex gate;
    std::list<std::shared_ptr<Waiter>> waiters;
    int count, maximum, initial;
    bool fifo, deleted = false;
    Semaphore(int value, int limit, bool ordered) : count(value), maximum(limit), initial(value), fifo(ordered) {}
};
struct Registry {
    std::mutex gate;
    Handle next = 0x534d000000000001ull;
    std::map<Handle, std::shared_ptr<Semaphore>> semaphores;
} registry;
std::atomic<bool> fail_queue{}; // host-test injection before a waiter is published
std::atomic<std::uint64_t> wait_sequence{}, signal_sequence{};

template<typename Function> __declspec(noinline) int Api(const void* caller, Function&& function) {
    try { return function(); }
    catch (const std::bad_alloc&) { return kMemory; }
    catch (...) { UnsupportedApi("native_semaphore_host_failure", 0, caller); }
}
std::string QuoteBytes(std::string_view text) {
    constexpr char hex[] = "0123456789abcdef";
    std::string output = "\"";
    for (const auto c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') { output += '\\'; output += c; }
        else if (byte < 32 || byte >= 127) { output += "\\u00"; output += hex[byte >> 4]; output += hex[byte & 15]; }
        else { output += c; }
    }
    output += '"'; return output;
}
std::shared_ptr<Semaphore> Find(Handle id) {
    struct Cached { Handle id{}; std::weak_ptr<Semaphore> value; };
    // Work/done pairs dominate the hot path. Weak ownership does not keep a
    // deleted semaphore alive; every operation still checks deleted under gate.
    thread_local std::array<Cached, 2> recent;
    thread_local unsigned replacement{};
    for (const auto& cached : recent) {
        if (cached.id == id) { if (auto value = cached.value.lock()) { return value; } }
    }
    std::lock_guard lock(registry.gate);
    const auto found = registry.semaphores.find(id);
    if (found == registry.semaphores.end()) { return nullptr; }
    recent[replacement++ % recent.size()] = {id, found->second};
    return found->second;
}
int Create(Handle* output, const char* name, unsigned attributes, int initial, int maximum, const void* options, const void* caller) {
    return Api(caller, [&] {
        if (!output) { return kAddress; }
        if (!name || attributes > 2 || initial < 0 || maximum <= 0 || initial > maximum) { return kInvalid; }
        if (options) { UnsupportedApi("sceKernelCreateSema_options", reinterpret_cast<std::uintptr_t>(options), caller); }
        std::size_t length = 0; while (length < 32 && name[length]) { ++length; }
        if (length == 32) { return kName; }
        const auto quoted = QuoteBytes({name, length});
        auto semaphore = std::make_shared<Semaphore>(initial, maximum, attributes == 1);
        Handle id;
        {
            std::lock_guard lock(registry.gate);
            if (registry.semaphores.size() >= 65535 || registry.next == UINT64_MAX) { return kAgain; }
            id = registry.next; TraceNativeApi("sceKernelCreateSema", id, caller);
            registry.semaphores.emplace(id, std::move(semaphore)); ++registry.next; *output = id;
        }
        std::printf("{\"event\":\"native_semaphore_create\",\"handle\":%llu,\"name_bytes\":%s,\"attributes\":%u,\"initial\":%d,\"maximum\":%d}\n",
            id, quoted.c_str(), attributes, initial, maximum);
        return 0;
    });
}
// Grant complete requests, in queue order among satisfiable waiters. Credits
// are reserved under the gate before a thread is woken; a newcomer cannot
// steal a completed wait's credit before that waiter gets scheduled again.
void Grant(Semaphore& semaphore, Notifications& notifications, const void* caller = nullptr) {
    std::uint64_t granted_at{};
    for (auto entry = semaphore.waiters.begin(); entry != semaphore.waiters.end();) {
        auto waiter = *entry;
        if (waiter->requested > semaphore.count) { ++entry; continue; }
        semaphore.count -= waiter->requested; entry = semaphore.waiters.erase(entry);
        if (!granted_at) { granted_at = kt::diagnostics::MonotonicMicroseconds(); }
        waiter->timing.granted_clock_us = granted_at;
        waiter->timing.grant_thread = GetCurrentThreadId();
        waiter->timing.grant_caller = caller;
        notifications.Add(waiter); waiter->result.store(0, std::memory_order_release);
    }
}
int Wait(Handle id, int requested, unsigned* timeout, bool blocking, const void* caller, int fixture_priority = -1,
    NativeSemaphoreWaitTiming* fixture_timing = nullptr) {
    NativeHostOperationScope timing("sceKernelWaitSema", "synchronization", static_cast<std::uint64_t>(requested), caller, blocking);
    return Api(caller, [&] {
        const auto lookup_began = kt::diagnostics::MonotonicMicroseconds();
        const auto semaphore = Find(id); if (!semaphore) { return kSearch; }
        const auto priority = fixture_priority >= 0 ? fixture_priority : NativeGuestThreadPriority();
        const auto gate_began = kt::diagnostics::MonotonicMicroseconds();
        std::unique_lock lock(semaphore->gate);
        const auto gate_acquired = kt::diagnostics::MonotonicMicroseconds();
        if (semaphore->deleted) { return kSearch; }
        if (requested <= 0 || requested > semaphore->maximum) { return kInvalid; }
        if (semaphore->count >= requested) { semaphore->count -= requested; return 0; }
        if (!blocking) { return kBusy; }
        const unsigned budget = timeout ? *timeout : 0;
        if (timeout && !budget) { return kTimeout; }
        const auto waiter = AcquireWaiter(requested, priority);
        auto insertion = semaphore->waiters.end();
        if (!semaphore->fifo) {
            insertion = semaphore->waiters.begin();
            while (insertion != semaphore->waiters.end() && (*insertion)->priority <= priority) { ++insertion; }
        }
        if (fail_queue.exchange(false)) { throw std::bad_alloc(); }
        const auto sequence = wait_sequence.fetch_add(1, std::memory_order_relaxed);
        if (sequence < 16) { TraceNativeApi("sceKernelWaitSema", id, caller); }
        const auto queued = semaphore->waiters.insert(insertion, waiter);
        const auto start = std::chrono::steady_clock::now();
        waiter->timing.queued_clock_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(start.time_since_epoch()).count());
        waiter->timing.wait_sequence = sequence;
        waiter->timing.lookup_us = gate_began - lookup_began;
        waiter->timing.gate_wait_us = gate_acquired - gate_began;
        waiter->timing.queued_under_gate_us = waiter->timing.queued_clock_us - gate_acquired;
        waiter->timing.queued_waiters = static_cast<unsigned>(semaphore->waiters.size());
        waiter->timing.available_credits = static_cast<unsigned>(semaphore->count);
        if (sequence < 16) {
            std::printf("{\"event\":\"native_semaphore_wait_begin\",\"handle\":%llu,\"requested\":%d,\"timed\":%s,\"timeout_us\":%u,\"sequence\":%llu}\n",
                id, requested, timeout ? "true" : "false", budget, sequence);
        }
        try {
            if (waiter->primitive != WaitPrimitive::ConditionVariable) {
                // Credits/result are published under gate, but success no longer
                // reacquires that shared gate. Only timeout retirement needs it.
                const auto deadline = start + std::chrono::microseconds(budget);
                lock.unlock();
                while (waiter->result.load(std::memory_order_acquire) == kPending) {
                    DWORD milliseconds = INFINITE;
                    if (timeout) {
                        const auto now = std::chrono::steady_clock::now();
                        if (now >= deadline) {
                            lock.lock();
                            if (waiter->result.load(std::memory_order_acquire) == kPending) {
                                semaphore->waiters.erase(queued);
                                waiter->result.store(kTimeout, std::memory_order_release);
                            }
                            lock.unlock(); break;
                        }
                        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
                        milliseconds = static_cast<DWORD>(remaining);
                    }
                    if (waiter->primitive == WaitPrimitive::Event) {
                        waiter->timing.park_began_clock_us = kt::diagnostics::MonotonicMicroseconds();
                        ++waiter->timing.park_calls;
                        const auto status = WaitForSingleObject(waiter->event, milliseconds);
                        const auto error = status == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
                        waiter->timing.park_returned_clock_us = kt::diagnostics::MonotonicMicroseconds();
                        if (status != WAIT_OBJECT_0 && status != WAIT_TIMEOUT) {
                            UnsupportedApi("native_semaphore_event_wait_failure", error, caller);
                        }
                    } else {
                        int pending = kPending;
                        waiter->timing.park_began_clock_us = kt::diagnostics::MonotonicMicroseconds();
                        ++waiter->timing.park_calls;
                        const auto status = WaitOnAddress(&waiter->result, &pending, sizeof(pending), milliseconds);
                        const auto error = status ? ERROR_SUCCESS : GetLastError();
                        waiter->timing.park_returned_clock_us = kt::diagnostics::MonotonicMicroseconds();
                        if (!status && error != ERROR_TIMEOUT) {
                            UnsupportedApi("native_semaphore_address_wait_failure", error, caller);
                        }
                    }
                    // Spurious/early wakes recheck the predicate and original
                    // deadline; a wake that precedes parking is not lost.
                }
            } else if (timeout) {
                const auto deadline = start + std::chrono::microseconds(budget);
                waiter->timing.park_began_clock_us = kt::diagnostics::MonotonicMicroseconds();
                ++waiter->timing.park_calls;
                if (!waiter->ready.wait_until(lock, deadline, [&] { return waiter->result != kPending; })) {
                    semaphore->waiters.erase(queued); waiter->result = kTimeout;
                }
                waiter->timing.park_returned_clock_us = kt::diagnostics::MonotonicMicroseconds();
            } else {
                waiter->timing.park_began_clock_us = kt::diagnostics::MonotonicMicroseconds();
                ++waiter->timing.park_calls;
                waiter->ready.wait(lock, [&] { return waiter->result != kPending; });
                waiter->timing.park_returned_clock_us = kt::diagnostics::MonotonicMicroseconds();
            }
        } catch (...) {
            // Do not unwind an incomplete queue transaction on host failure.
            UnsupportedApi("native_semaphore_wait_host_failure", id, caller);
        }
        waiter->timing.resumed_clock_us = kt::diagnostics::MonotonicMicroseconds();
        waiter->timing.wake_finished_clock_us = waiter->wake_finished.load(std::memory_order_acquire);
        waiter->timing.wake_began_clock_us = waiter->wake_began.load(std::memory_order_acquire);
        waiter->timing.address_wait = waiter->primitive == WaitPrimitive::Address;
        waiter->timing.event_wait = waiter->primitive == WaitPrimitive::Event;
        const auto result = waiter->result.load(std::memory_order_acquire);
        if (timeout) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
            *timeout = result || elapsed >= budget ? 0 : budget - static_cast<unsigned>(elapsed);
        }
        if (sequence < 16) {
            std::printf("{\"event\":\"native_semaphore_wait_end\",\"handle\":%llu,\"result\":%d,\"sequence\":%llu}\n", id, result, sequence);
        }
        if (fixture_timing) { *fixture_timing = waiter->timing; }
        // Never format/log while holding the gate being diagnosed. Keep slow
        // waits bounded, while preserving the existing generic scope counters.
        if (lock.owns_lock()) { lock.unlock(); }
        // A worker parked until the next frame is expected, not a slow work
        // item. Keep batched completion waits and late wakeups, rather than
        // logging every idle worker in a large guest pool eight times/second.
        const auto waited_us = waiter->timing.resumed_clock_us - waiter->timing.queued_clock_us;
        const auto late_resume = waiter->timing.granted_clock_us &&
            waiter->timing.resumed_clock_us - waiter->timing.granted_clock_us >= 2000;
        if ((requested > 1 && waited_us >= 1000) || late_resume || (result && waited_us >= 4000)) {
            thread_local std::uint64_t report_second = UINT64_MAX;
            thread_local unsigned reports{};
            const auto second = waiter->timing.resumed_clock_us / 1000000;
            if (second != report_second) { report_second = second; reports = 0; }
            if (reports++ < 8) { EmitNativeSemaphoreWait(id, requested, result, caller, waiter->timing); }
        }
        return result;
    });
}
int Signal(Handle id, int count, const void* caller) {
    return Api(caller, [&] {
        const auto semaphore = Find(id); if (!semaphore) { return kSearch; }
        Notifications notifications;
        std::unique_lock lock(semaphore->gate);
        if (semaphore->deleted) { return kSearch; }
        if (count <= 0 || count > semaphore->maximum - semaphore->count) { return kInvalid; }
        semaphore->count += count; Grant(*semaphore, notifications, caller);
        if (signal_sequence.fetch_add(1, std::memory_order_relaxed) < 16) {
            std::printf("{\"event\":\"native_semaphore_signal\",\"handle\":%llu,\"added\":%d,\"remaining\":%d,\"waiters\":%zu}\n", id, count, semaphore->count, semaphore->waiters.size());
        }
        lock.unlock(); notifications.NotifyAll();
        return 0;
    });
}
void RetireWaiters(Semaphore& semaphore, int result, Notifications& notifications) {
    for (const auto& waiter : semaphore.waiters) { notifications.Add(waiter); waiter->result.store(result, std::memory_order_release); }
    semaphore.waiters.clear();
}
int Cancel(Handle id, int reset, int* waiters, const void* caller) {
    return Api(caller, [&] {
        const auto semaphore = Find(id); if (!semaphore) { return kSearch; }
        Notifications notifications;
        std::unique_lock lock(semaphore->gate);
        if (semaphore->deleted) { return kSearch; }
        if (reset < -1 || reset > semaphore->maximum) { return kInvalid; }
        if (waiters) { *waiters = static_cast<int>(semaphore->waiters.size()); }
        RetireWaiters(*semaphore, kCancelled, notifications); semaphore->count = reset < 0 ? semaphore->initial : reset;
        lock.unlock(); notifications.NotifyAll(); return 0;
    });
}
int Delete(Handle id, const void* caller) {
    return Api(caller, [&] {
        Notifications notifications;
        std::unique_lock registry_lock(registry.gate);
        const auto found = registry.semaphores.find(id); if (found == registry.semaphores.end()) { return kSearch; }
        const auto semaphore = found->second;
        std::unique_lock state_lock(semaphore->gate);
        semaphore->deleted = true; RetireWaiters(*semaphore, kDeleted, notifications); registry.semaphores.erase(found);
        state_lock.unlock(); registry_lock.unlock(); notifications.NotifyAll(); return 0;
    });
}
int KT_SYSV KernelCreate(Handle* output, const char* name, unsigned attributes, int initial, int maximum, const void* options) {
    return Create(output, name, attributes, initial, maximum, options, __builtin_return_address(0));
}
int KT_SYSV KernelWait(Handle id, int count, unsigned* timeout) { return Wait(id, count, timeout, true, __builtin_return_address(0)); }
int KT_SYSV KernelPoll(Handle id, int count) { return Wait(id, count, nullptr, false, __builtin_return_address(0)); }
int KT_SYSV KernelSignal(Handle id, int count) { return Signal(id, count, __builtin_return_address(0)); }
int KT_SYSV KernelCancel(Handle id, int reset, int* waiters) { return Cancel(id, reset, waiters, __builtin_return_address(0)); }
int KT_SYSV KernelDelete(Handle id) { return Delete(id, __builtin_return_address(0)); }
}

std::uint64_t ResolveSemaphoreImport(std::string_view identifier) {
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "188x57JYp0g") { return reinterpret_cast<std::uint64_t>(&KernelCreate); }
    if (id == "Zxa0VhQVTsk") { return reinterpret_cast<std::uint64_t>(&KernelWait); }
    if (id == "4czppHBiriw") { return reinterpret_cast<std::uint64_t>(&KernelSignal); }
    if (id == "12wOHk8ywb0") { return reinterpret_cast<std::uint64_t>(&KernelPoll); }
    if (id == "4DM06U2BNEY") { return reinterpret_cast<std::uint64_t>(&KernelCancel); }
    if (id == "R1Jvn8bSCW8") { return reinterpret_cast<std::uint64_t>(&KernelDelete); }
    return 0;
}

bool ConfigureNativeSemaphoreWait(WaitPrimitive primitive) {
    std::lock_guard lock(registry.gate);
    if (registry.next != 0x534d000000000001ull) { return false; }
    if (primitive != WaitPrimitive::ConditionVariable && primitive != WaitPrimitive::Address && primitive != WaitPrimitive::Event) { return false; }
    wait_primitive.store(primitive); return true;
}
NativeSemaphoreWaitPrimitive NativeSemaphoreWaitMode() { return wait_primitive.load(); }
const char* NativeSemaphoreWaitName() {
    switch (wait_primitive.load()) {
    case WaitPrimitive::Event: return "event";
    case WaitPrimitive::Address: return "address";
    default: return "cv";
    }
}

static bool SemaphoreSelfTestForPrimitive(WaitPrimitive primitive) {
    wait_primitive = primitive;
    using namespace std::chrono_literals;
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        std::printf("{\"event\":\"semaphore_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false"); passed &= value;
    };
    const auto create = [](Handle& id, int initial, int maximum, unsigned attributes = 1) {
        return Create(&id, "AuthoredSema", attributes, initial, maximum, nullptr, nullptr);
    };
    const auto await = [](const auto& predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!predicate() && std::chrono::steady_clock::now() < deadline) { Sleep(1); }
        return predicate();
    };
    const auto queued = [&](Handle id, std::size_t count) {
        const auto semaphore = Find(id); if (!semaphore) { return false; }
        return await([&] { std::lock_guard lock(semaphore->gate); return semaphore->waiters.size() == count; });
    };
    const auto timed = [](Handle id, int count = 1, int priority = -1) {
        unsigned budget = 2000000; return Wait(id, count, &budget, true, nullptr, priority);
    };
    struct { Handle value = 73; std::uint64_t sentinel = 73; } result;
    check("creation_validates_bounds_before_writing_output", Create(nullptr, "test", 1, 0, 1, nullptr, nullptr) == kAddress &&
          Create(&result.value, nullptr, 1, 0, 1, nullptr, nullptr) == kInvalid && create(result.value, -1, 1) == kInvalid &&
          create(result.value, 2, 1) == kInvalid && create(result.value, 0, 0) == kInvalid && create(result.value, 0, 1, 3) == kInvalid && result.value == 73);
    check("pointer_sized_handle_writes_eight_bytes_and_preserves_tail", create(result.value, 2, 5) == 0 && result.value > UINT32_MAX && result.sentinel == 73);
    const auto id = result.value;
    check("wait_primitive_cannot_change_after_semaphore_publication", !ConfigureNativeSemaphoreWait(WaitPrimitive::ConditionVariable) && NativeSemaphoreWaitMode() == primitive);
    {
        auto previous = AcquireWaiter(1, 700);
        const auto identity = previous.get();
        previous->Wake(); // A result may be observed before the old wait parks.
        previous.reset();
        auto reused = AcquireWaiter(1, 700);
        check("exclusive_waiter_reuse_clears_an_unconsumed_event_signal", reused.get() == identity &&
            reused->result == kPending && (!reused->event || WaitForSingleObject(reused->event, 0) == WAIT_TIMEOUT));
    }
    Handle absent = 73;
    check("bounded_opaque_name_and_safe_json_escaping", Create(&absent, std::string(32, 'a').c_str(), 1, 0, 1, nullptr, nullptr) == kName && absent == 73 &&
          QuoteBytes(std::string_view("\xff\n\"", 3)) == "\"\\u00ff\\u000a\\\"\"");
    unsigned budget = 12345;
    check("immediate_acquisition_consumes_exact_count_without_changing_timeout", Wait(id, 2, &budget, true, nullptr) == 0 && budget == 12345 &&
          Wait(id, 1, nullptr, false, nullptr) == kBusy);
    check("invalid_wait_and_signal_counts_never_change_counter", Wait(id, 0, nullptr, true, nullptr) == kInvalid && Wait(id, -1, nullptr, true, nullptr) == kInvalid &&
          Wait(id, 6, nullptr, true, nullptr) == kInvalid && Signal(id, 0, nullptr) == kInvalid && Signal(id, -1, nullptr) == kInvalid &&
          Signal(id, 5, nullptr) == 0 && Signal(id, 1, nullptr) == kInvalid && Wait(id, 5, nullptr, false, nullptr) == 0);
    budget = 0;
    check("zero_timeout_only_times_out_if_no_credit_is_available", Wait(id, 1, &budget, true, nullptr) == kTimeout && Find(id)->waiters.empty() &&
          Signal(id, 1, nullptr) == 0 && Wait(id, 1, &budget, true, nullptr) == 0 && budget == 0);
    const auto started = std::chrono::steady_clock::now(); budget = 20000;
    const auto timeout_result = Wait(id, 1, &budget, true, nullptr);
    check("finite_timeout_really_waits_and_retires_queue_entry", timeout_result == kTimeout && budget == 0 &&
          std::chrono::steady_clock::now() - started >= 15ms && Find(id)->waiters.empty() && Signal(id, 1, nullptr) == 0 && Wait(id, 1, nullptr, false, nullptr) == 0);
    fail_queue = true; budget = 12345;
    check("allocation_failure_does_not_leave_dangling_waiter_or_change_timeout", Wait(id, 1, &budget, true, nullptr) == kMemory && budget == 12345 &&
          Find(id)->waiters.empty() && Find(id)->count == 0);
    fail_queue = false;
    {
        std::atomic<int> value{kPending};
        std::thread worker([&] { value = Wait(id, 1, nullptr, true, nullptr); });
        const auto waiting = queued(id, 1); Signal(id, 1, nullptr); worker.join();
        check("infinite_wait_is_woken_by_actual_signal_and_credit_is_reserved", waiting && value == 0 && Wait(id, 1, nullptr, false, nullptr) == kBusy);
    }
    {
        std::atomic<int> value{kPending};
        std::thread worker([&] { value = timed(id, 3); });
        const auto waiting = queued(id, 1); Signal(id, 2, nullptr);
        bool not_partial{};
        { const auto state = Find(id); std::lock_guard lock(state->gate); not_partial = state->count == 2 && state->waiters.size() == 1; }
        Signal(id, 1, nullptr); worker.join();
        check("batch_wait_does_not_partially_consume_insufficient_signal", waiting && not_partial && value == 0 && Wait(id, 1, nullptr, false, nullptr) == kBusy);
    }
    {
        Handle delayed{}; create(delayed, 0, 2); unsigned remaining = 200000; std::atomic<int> value{kPending};
        NativeSemaphoreWaitTiming timing;
        std::thread worker([&] { value = Wait(delayed, 1, &remaining, true, nullptr, -1, &timing); });
        const auto waiting = queued(delayed, 1);
        Notifications notifications;
        {
            const auto state = Find(delayed); std::lock_guard lock(state->gate);
            ++state->count; Grant(*state, notifications); Sleep(250); // force scheduling after the timeout budget
        }
        notifications.NotifyAll();
        worker.join();
        // Address waits may return early without a notifier: the already
        // published success is valid, with a small positive budget left. The
        // notifier's Sleep does not force the waiting thread past its deadline.
        const auto elapsed = timing.resumed_clock_us - timing.queued_clock_us;
        const bool budget_valid = remaining <= 200000 &&
            (elapsed >= 200000 ? remaining == 0 : remaining <= 200000 - elapsed);
        // Cleanup must not depend on an earlier assertion succeeding, otherwise
        // one timing failure poisons the registry check of later primitives.
        const auto deleted = Delete(delayed, nullptr);
        std::printf("{\"event\":\"semaphore_timeout_fixture\",\"primitive\":\"%s\",\"elapsed_us\":%llu,\"remaining_us\":%u,\"deleted\":%d}\n",
            NativeSemaphoreWaitName(),static_cast<unsigned long long>(elapsed),remaining,deleted);
        check("reserved_signal_wins_race_and_remaining_timeout_matches_actual_elapsed_time", waiting && value == 0 && budget_valid && deleted == 0);
        check("grant_to_resume_delay_is_distinct_from_waiting_for_a_signal", timing.grant_thread == GetCurrentThreadId() &&
            timing.queued_clock_us <= timing.granted_clock_us && timing.resumed_clock_us - timing.granted_clock_us >= 150000);
        check("waiter_park_return_is_separate_from_notifier_wake_return", timing.park_calls >= 1 &&
            timing.queued_clock_us <= timing.park_began_clock_us && timing.park_began_clock_us <= timing.park_returned_clock_us &&
            timing.park_returned_clock_us <= timing.resumed_clock_us && timing.queued_waiters == 1 && timing.available_credits == 0 &&
            timing.address_wait == (primitive == WaitPrimitive::Address) && timing.event_wait == (primitive == WaitPrimitive::Event));
    }
    {
        bool ordered = true;
        for (const unsigned attributes : {1u, 2u, 0u}) {
            Handle selected{}; create(selected, 0, 4, attributes);
            std::atomic<int> older{kPending}, higher{kPending};
            std::thread first([&] { older = timed(selected, 1, 700); });
            ordered &= queued(selected, 1);
            std::thread second([&] { higher = timed(selected, 1, 699); });
            ordered &= queued(selected, 2); Signal(selected, 1, nullptr);
            ordered &= await([&] { return older != kPending || higher != kPending; });
            ordered &= attributes == 1 ? older == 0 && higher == kPending : higher == 0 && older == kPending;
            int cancelled = -1; Cancel(selected, 0, &cancelled, nullptr); first.join(); second.join();
            ordered &= cancelled == 1 && (attributes == 1 ? higher == kCancelled : older == kCancelled) && Delete(selected, nullptr) == 0;
        }
        check("fifo_and_priority_queue_orders_are_distinct", ordered);
    }
    {
        Handle selected{}; create(selected, 0, 2);
        std::atomic<int> value{kPending};
        std::thread worker([&] { value = timed(selected); });
        const auto waiting = queued(selected, 1);
        bool expected_resume{};
        {
            const auto state = Find(selected); std::lock_guard lock(state->gate);
            Notifications notifications;
            ++state->count; Grant(*state, notifications); notifications.NotifyAll();
            if (primitive != WaitPrimitive::ConditionVariable) { expected_resume = await([&] { return value == 0; }); }
            else { Sleep(5); expected_resume = value == kPending; }
        }
        worker.join();
        check("published_success_resumes_without_reacquiring_gate_while_cv_requires_it", waiting && expected_resume && value == 0 && Delete(selected, nullptr) == 0);
    }
    {
        Handle selected{}; create(selected, 0, 2);
        std::atomic<int> value{kPending};
        std::thread worker([&] { value = timed(selected); });
        const auto waiting = queued(selected, 1);
        Notifications notifications; std::weak_ptr<Waiter> lifetime;
        {
            const auto state = Find(selected); std::lock_guard lock(state->gate);
            if (waiting) { lifetime = state->waiters.front(); ++state->count; Grant(*state, notifications); }
        }
        // Force return before the production notifier finishes. The lease, not
        // the waiting thread or its TLS cache, must still own this CV.
        if (notifications.first) { notifications.first->Wake(); }
        worker.join();
        const bool retained = !lifetime.expired(); notifications.NotifyAll();
        check("notification_lease_keeps_cv_alive_after_waiting_thread_exits", waiting && value == 0 && retained && lifetime.expired() && Delete(selected, nullptr) == 0);
    }
    {
        Handle selected{}; create(selected, 0, 2);
        std::atomic<int> first{kPending}, second{kPending};
        std::thread worker([&] { first = timed(selected); second = timed(selected); });
        const auto waiting = queued(selected, 1);
        Notifications notifications; std::weak_ptr<Waiter> first_waiter;
        {
            const auto state = Find(selected); std::lock_guard lock(state->gate);
            if (waiting) { first_waiter = state->waiters.front(); ++state->count; Grant(*state, notifications); }
        }
        if (notifications.first) { notifications.first->Wake(); }
        const bool requeued = await([&] {
            const auto state = Find(selected); std::lock_guard lock(state->gate);
            return first == 0 && state->waiters.size() == 1;
        });
        bool distinct{};
        {
            const auto state = Find(selected); std::lock_guard lock(state->gate);
            if (requeued) { distinct = state->waiters.front() != first_waiter.lock(); }
        }
        Signal(selected, 1, nullptr); worker.join(); notifications.NotifyAll();
        check("next_wait_never_reuses_a_cv_still_owned_by_previous_notifier", waiting && requeued && distinct && second == 0 && first_waiter.expired() && Delete(selected, nullptr) == 0);
    }
    {
        Handle selected{}; create(selected, 0, 2, 2);
        std::atomic<int> older{kPending}, newer{kPending};
        std::thread first([&] { older = timed(selected, 1, 700); });
        const auto first_queued = queued(selected, 1);
        std::thread second([&] { newer = timed(selected, 1, 700); });
        const auto both_queued = queued(selected, 2); Signal(selected, 1, nullptr);
        const auto woke = await([&] { return older != kPending || newer != kPending; });
        const bool stable = older == 0 && newer == kPending;
        Cancel(selected, 0, nullptr, nullptr); first.join(); second.join();
        check("equal_priority_waiters_keep_registration_order", first_queued && both_queued && woke && stable && newer == kCancelled && Delete(selected, nullptr) == 0);
    }
    {
        Handle selected{}; create(selected, 0, 8); std::atomic<int> large{kPending}, small{kPending};
        std::thread first([&] { large = timed(selected, 3); });
        const auto first_queued = queued(selected, 1);
        std::thread second([&] { small = timed(selected, 1); });
        const auto both_queued = queued(selected, 2); Signal(selected, 1, nullptr);
        const auto eligible = await([&] { return small != kPending; });
        const bool skipped = small == 0 && large == kPending;
        Cancel(selected, 0, nullptr, nullptr); first.join(); second.join();
        check("eligible_waiter_can_use_credit_insufficient_for_an_earlier_batch", first_queued && both_queued && eligible && skipped && large == kCancelled && Delete(selected, nullptr) == 0);
    }
    {
        Handle cancelled{}; create(cancelled, 2, 8); Wait(cancelled, 2, nullptr, false, nullptr);
        unsigned remaining = 2000000; std::atomic<int> value{kPending};
        std::thread worker([&] { value = Wait(cancelled, 5, &remaining, true, nullptr); });
        const auto waiting = queued(cancelled, 1);
        struct { int count = 73; unsigned sentinel = 73; } waiting_count;
        const bool rejected = Cancel(cancelled, 9, &waiting_count.count, nullptr) == kInvalid && waiting_count.count == 73;
        const auto reset = Cancel(cancelled, -1, &waiting_count.count, nullptr); worker.join();
        check("cancel_reports_waiter_count_restores_initial_value_and_preserves_tail", waiting && rejected && reset == 0 && value == kCancelled && remaining == 0 &&
              waiting_count.count == 1 && waiting_count.sentinel == 73 && Wait(cancelled, 2, nullptr, false, nullptr) == 0 &&
              Signal(cancelled, 1, nullptr) == 0 && Wait(cancelled, 1, nullptr, false, nullptr) == 0 && Delete(cancelled, nullptr) == 0);
    }
    {
        Handle removed{}; create(removed, 0, 3); std::atomic<int> first{kPending}, second{kPending};
        std::thread one([&] { first = timed(removed); }); std::thread two([&] { second = timed(removed); });
        const auto waiting = queued(removed, 2); const auto erased = Delete(removed, nullptr); one.join(); two.join();
        Handle replacement{}; create(replacement, 1, 3);
        check("delete_wakes_existing_waiters_and_stale_handle_cannot_touch_replacement", waiting && erased == 0 && first == kDeleted && second == kDeleted &&
              replacement != removed && Delete(removed, nullptr) == kSearch && Signal(removed, 1, nullptr) == kSearch && Wait(removed, 1, nullptr, false, nullptr) == kSearch &&
              Wait(replacement, 1, nullptr, false, nullptr) == 0 && Delete(replacement, nullptr) == 0);
    }
    {
        Handle maximum{}; create(maximum, INT32_MAX, INT32_MAX);
        check("signed_maximum_count_does_not_overflow_on_signal_or_batch_acquire", Signal(maximum, 1, nullptr) == kInvalid && Wait(maximum, INT32_MAX, nullptr, false, nullptr) == 0 &&
              Signal(maximum, INT32_MAX, nullptr) == 0 && Wait(maximum, INT32_MAX, nullptr, false, nullptr) == 0 && Delete(maximum, nullptr) == 0);
    }
    {
        Handle removed{}; create(removed, 1, 2);
        auto retained = Find(removed); std::weak_ptr<Semaphore> lifetime = retained;
        const bool cached = Wait(removed, 1, nullptr, false, nullptr) == 0;
        std::atomic<int> erased{kPending};
        std::thread deleter([&] { erased = Delete(removed, nullptr); }); deleter.join();
        int count = 73;
        const bool rejected = Wait(removed, 1, nullptr, false, nullptr) == kSearch && Signal(removed, 1, nullptr) == kSearch &&
            Cancel(removed, 0, &count, nullptr) == kSearch && count == 73;
        retained.reset();
        check("cached_handle_observes_cross_thread_delete_without_retaining_object", cached && erased == 0 && rejected && lifetime.expired());
    }
    {
        Handle ready{}, done{}; create(ready, 0, 1); create(done, 0, 1);
        std::atomic<bool> failed{}; unsigned exchanges = 0;
        std::thread worker([&] {
            for (unsigned i = 0; i < 200 && !failed; ++i) {
                if (timed(ready) || Signal(done, 1, nullptr)) { failed = true; break; } ++exchanges;
            }
            if (failed) { Cancel(ready, 0, nullptr, nullptr); Cancel(done, 0, nullptr, nullptr); }
        });
        for (unsigned i = 0; i < 200 && !failed; ++i) { if (Signal(ready, 1, nullptr) || timed(done)) { failed = true; break; } }
        if (failed) { Cancel(ready, 0, nullptr, nullptr); Cancel(done, 0, nullptr, nullptr); }
        worker.join();
        check("two_hundred_producer_consumer_round_trips_keep_exact_credits", !failed && exchanges == 200 && Delete(ready, nullptr) == 0 && Delete(done, nullptr) == 0);
    }
    bool cycles = Delete(id, nullptr) == 0; auto previous = id;
    for (unsigned i = 0; i < 128; ++i) {
        Handle current{}; cycles &= create(current, 0, 1) == 0 && current > previous && Delete(current, nullptr) == 0; previous = current;
    }
    check("repeated_create_delete_does_not_reuse_stale_identity", cycles && registry.semaphores.empty());
    check("unknown_handles_rejected_by_every_operation", Wait(73, 1, nullptr, false, nullptr) == kSearch && Signal(73, 1, nullptr) == kSearch &&
          Cancel(73, 0, nullptr, nullptr) == kSearch && Delete(73, nullptr) == kSearch);
    std::printf("{\"event\":\"semaphore_primitive_self_test\",\"primitive\":\"%s\",\"passed\":%s}\n", NativeSemaphoreWaitName(), passed ? "true" : "false"); return passed;
}
bool SemaphoreSelfTest() {
    const auto original = wait_primitive.load();
    const bool cv = SemaphoreSelfTestForPrimitive(WaitPrimitive::ConditionVariable);
    const bool address = SemaphoreSelfTestForPrimitive(WaitPrimitive::Address);
    const bool event = SemaphoreSelfTestForPrimitive(WaitPrimitive::Event);
    wait_primitive = original;
    const bool passed = cv && address && event;
    std::printf("{\"event\":\"semaphore_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
