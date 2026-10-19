#include "native_semaphore.h"
#include "native_diagnostics.h"
#include "native_host_timing.h"
#include "native_threads.h"
#include <windows.h>
#include <algorithm>
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
struct Waiter {
    std::condition_variable ready;
    int requested, priority, result = kPending;
};
struct Semaphore {
    std::mutex gate;
    std::list<Waiter*> waiters;
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
    std::lock_guard lock(registry.gate);
    const auto found = registry.semaphores.find(id); return found == registry.semaphores.end() ? nullptr : found->second;
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
void Grant(Semaphore& semaphore) {
    for (auto entry = semaphore.waiters.begin(); entry != semaphore.waiters.end();) {
        auto* waiter = *entry;
        if (waiter->requested > semaphore.count) { ++entry; continue; }
        semaphore.count -= waiter->requested; entry = semaphore.waiters.erase(entry);
        waiter->result = 0; waiter->ready.notify_one();
    }
}
int Wait(Handle id, int requested, unsigned* timeout, bool blocking, const void* caller, int fixture_priority = -1) {
    NativeHostOperationScope timing("sceKernelWaitSema", "synchronization", static_cast<std::uint64_t>(requested), caller, blocking);
    return Api(caller, [&] {
        const auto semaphore = Find(id); if (!semaphore) { return kSearch; }
        const auto priority = fixture_priority >= 0 ? fixture_priority : NativeGuestThreadPriority();
        std::unique_lock lock(semaphore->gate);
        if (semaphore->deleted) { return kSearch; }
        if (requested <= 0 || requested > semaphore->maximum) { return kInvalid; }
        if (semaphore->count >= requested) { semaphore->count -= requested; return 0; }
        if (!blocking) { return kBusy; }
        const unsigned budget = timeout ? *timeout : 0;
        if (timeout && !budget) { return kTimeout; }
        Waiter waiter{{}, requested, priority};
        auto insertion = semaphore->waiters.end();
        if (!semaphore->fifo) {
            insertion = semaphore->waiters.begin();
            while (insertion != semaphore->waiters.end() && (*insertion)->priority <= priority) { ++insertion; }
        }
        if (fail_queue.exchange(false)) { throw std::bad_alloc(); }
        const auto sequence = wait_sequence.fetch_add(1, std::memory_order_relaxed);
        if (sequence < 16) { TraceNativeApi("sceKernelWaitSema", id, caller); }
        const auto queued = semaphore->waiters.insert(insertion, &waiter);
        const auto start = std::chrono::steady_clock::now();
        if (sequence < 16) {
            std::printf("{\"event\":\"native_semaphore_wait_begin\",\"handle\":%llu,\"requested\":%d,\"timed\":%s,\"timeout_us\":%u,\"sequence\":%llu}\n",
                id, requested, timeout ? "true" : "false", budget, sequence);
        }
        try {
            if (timeout) {
                const auto deadline = start + std::chrono::microseconds(budget);
                if (!waiter.ready.wait_until(lock, deadline, [&] { return waiter.result != kPending; })) {
                    semaphore->waiters.erase(queued); waiter.result = kTimeout;
                }
            } else { waiter.ready.wait(lock, [&] { return waiter.result != kPending; }); }
        } catch (...) {
            // Do not unwind a queued stack waiter out from under a notifier.
            UnsupportedApi("native_semaphore_wait_host_failure", id, caller);
        }
        if (timeout) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
            *timeout = waiter.result || elapsed >= budget ? 0 : budget - static_cast<unsigned>(elapsed);
        }
        if (sequence < 16) {
            std::printf("{\"event\":\"native_semaphore_wait_end\",\"handle\":%llu,\"result\":%d,\"sequence\":%llu}\n", id, waiter.result, sequence);
        }
        return waiter.result;
    });
}
int Signal(Handle id, int count, const void* caller) {
    return Api(caller, [&] {
        const auto semaphore = Find(id); if (!semaphore) { return kSearch; }
        std::lock_guard lock(semaphore->gate);
        if (semaphore->deleted) { return kSearch; }
        if (count <= 0 || count > semaphore->maximum - semaphore->count) { return kInvalid; }
        semaphore->count += count; Grant(*semaphore);
        if (signal_sequence.fetch_add(1, std::memory_order_relaxed) < 16) {
            std::printf("{\"event\":\"native_semaphore_signal\",\"handle\":%llu,\"added\":%d,\"remaining\":%d,\"waiters\":%zu}\n", id, count, semaphore->count, semaphore->waiters.size());
        }
        return 0;
    });
}
void RetireWaiters(Semaphore& semaphore, int result) {
    for (auto* waiter : semaphore.waiters) { waiter->result = result; waiter->ready.notify_one(); }
    semaphore.waiters.clear();
}
int Cancel(Handle id, int reset, int* waiters, const void* caller) {
    return Api(caller, [&] {
        const auto semaphore = Find(id); if (!semaphore) { return kSearch; }
        std::lock_guard lock(semaphore->gate);
        if (semaphore->deleted) { return kSearch; }
        if (reset < -1 || reset > semaphore->maximum) { return kInvalid; }
        if (waiters) { *waiters = static_cast<int>(semaphore->waiters.size()); }
        RetireWaiters(*semaphore, kCancelled); semaphore->count = reset < 0 ? semaphore->initial : reset; return 0;
    });
}
int Delete(Handle id, const void* caller) {
    return Api(caller, [&] {
        std::lock_guard registry_lock(registry.gate);
        const auto found = registry.semaphores.find(id); if (found == registry.semaphores.end()) { return kSearch; }
        const auto semaphore = found->second;
        std::lock_guard state_lock(semaphore->gate);
        semaphore->deleted = true; RetireWaiters(*semaphore, kDeleted); registry.semaphores.erase(found); return 0;
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

bool SemaphoreSelfTest() {
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
        std::thread worker([&] { value = Wait(delayed, 1, &remaining, true, nullptr); });
        const auto waiting = queued(delayed, 1);
        {
            const auto state = Find(delayed); std::lock_guard lock(state->gate);
            ++state->count; Grant(*state); Sleep(250); // force scheduling after the timeout budget
        }
        worker.join();
        check("reserved_signal_wins_race_and_remaining_timeout_saturates_not_underflows", waiting && value == 0 && remaining == 0 && Delete(delayed, nullptr) == 0);
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
    std::printf("{\"event\":\"semaphore_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
