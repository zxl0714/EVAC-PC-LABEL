#include "native_sync.h"
#include "native_diagnostics.h"
#include "native_host_timing.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <climits>
#include <cstdio>
#include <limits>
#include <memory>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
// Orbis SCE error encoding; these are not Windows or Linux errno values.
constexpr int Error(unsigned number) { return static_cast<int>(0x80020000u | number); }
constexpr int kPermission = Error(1), kDeadlock = Error(11), kMemory = Error(12);
constexpr int kBusy = Error(16), kInvalid = Error(22), kAgain = Error(35), kUnsupported = Error(45);
constexpr int kTimeout = Error(60);

struct MutexState {
    explicit MutexState(unsigned value) : type(value) {}
    std::mutex gate;
    std::condition_variable changed;
    unsigned type;
    DWORD owner{};
    unsigned depth{};
    unsigned waiters{};
    unsigned condition_users{};
    bool destroyed{};
};
struct ConditionWaiter { std::condition_variable ready; bool notified{}; };
struct ConditionState {
    explicit ConditionState(unsigned clock_value) : clock(clock_value) {}
    std::mutex gate;
    std::list<ConditionWaiter*> queue;
    std::shared_ptr<MutexState> bound_mutex;
    unsigned clock, active{};
    bool destroyed{};
};

// The public objects are eight-byte opaque tokens, not host std::mutex layouts.
// Do not use this representation with an unmodified original libc that accesses
// kernel mutex internals. That module is not loaded by the current probe.
struct Registry {
    std::mutex gate;
    std::uint64_t next{0x10000};
    std::unordered_map<std::uint64_t, unsigned> attributes;
    std::unordered_map<std::uint64_t, std::shared_ptr<MutexState>> mutexes;
    std::unordered_map<std::uint64_t, unsigned> condition_attributes;
    std::unordered_map<std::uint64_t, std::shared_ptr<ConditionState>> conditions;
} registry;
std::atomic<bool> condition_queue_failure{}; // host self-test fault injection only

std::uint64_t NextToken() {
    if (registry.next == (std::numeric_limits<std::uint64_t>::max)()) {
        throw std::bad_alloc();
    }
    return registry.next++;
}

int KT_SYSV AttributeInit(std::uint64_t* slot) {
    if (!slot) { return kInvalid; }
    try {
        std::lock_guard lock(registry.gate);
        const auto token = NextToken();
        registry.attributes.emplace(token, 1); // Orbis default: error-checking
        *slot = token;
        return 0;
    } catch (...) { return kMemory; }
}

int KT_SYSV AttributeSetType(std::uint64_t* slot, unsigned type) {
    if (!slot || type < 1 || type > 4) { return kInvalid; }
    if (type == 4) { return kUnsupported; } // adaptive policy not implemented
    std::lock_guard lock(registry.gate);
    const auto found = registry.attributes.find(*slot);
    if (found == registry.attributes.end()) { return kInvalid; }
    found->second = type;
    return 0;
}

int KT_SYSV AttributeDestroy(std::uint64_t* slot) {
    if (!slot) { return kInvalid; }
    std::lock_guard lock(registry.gate);
    if (!registry.attributes.erase(*slot)) { return kInvalid; }
    *slot = 0;
    return 0;
}

int KT_SYSV MutexInit(std::uint64_t* slot, const std::uint64_t* attribute, const char*) {
    if (!slot) { return kInvalid; }
    try {
        std::lock_guard lock(registry.gate);
        if (registry.mutexes.contains(*slot)) { return kBusy; }
        unsigned type = 1;
        if (attribute) {
            const auto found = registry.attributes.find(*attribute);
            if (found == registry.attributes.end()) { return kInvalid; }
            type = found->second;
        }
        const auto token = NextToken();
        registry.mutexes.emplace(token, std::make_shared<MutexState>(type));
        *slot = token;
        std::printf("{\"event\":\"native_mutex_init\",\"type\":%u,\"token\":%llu}\n", type, token);
        return 0;
    } catch (...) { return kMemory; }
}

std::shared_ptr<MutexState> State(std::uint64_t* slot) {
    if (!slot) { return {}; }
    std::lock_guard lock(registry.gate);
    if (*slot == 0) {
        const auto token = NextToken();
        registry.mutexes.emplace(token, std::make_shared<MutexState>(1));
        *slot = token;
    }
    const auto found = registry.mutexes.find(*slot);
    return found == registry.mutexes.end() ? nullptr : found->second;
}

int Lock(std::uint64_t* slot, bool try_only) {
    try {
        const auto state = State(slot);
        if (!state) { return kInvalid; }
        std::unique_lock lock(state->gate);
        if (state->destroyed) { return kInvalid; }
        const auto thread = GetCurrentThreadId();
        if (state->owner == thread && state->type == 2) {
            if (state->depth == (std::numeric_limits<unsigned>::max)()) { return kAgain; }
            ++state->depth;
            return 0;
        }
        if (try_only && state->owner) { return kBusy; }
        if (state->owner == thread && state->type == 1) { return kDeadlock; }
        ++state->waiters;
        state->changed.wait(lock, [&] { return !state->owner || state->destroyed; });
        --state->waiters;
        if (state->destroyed) { return kInvalid; }
        state->owner = thread;
        state->depth = 1;
        return 0;
    } catch (...) { return kMemory; }
}

int KT_SYSV MutexLock(std::uint64_t* slot) {
    NativeHostOperationScope timing("scePthreadMutexLock", "synchronization", 0, __builtin_return_address(0));
    return Lock(slot, false);
}
int KT_SYSV MutexTryLock(std::uint64_t* slot) { return Lock(slot, true); }

int KT_SYSV MutexUnlock(std::uint64_t* slot) {
    try {
        const auto state = State(slot);
        if (!state) { return kInvalid; }
        std::lock_guard lock(state->gate);
        if (state->destroyed) { return kInvalid; }
        if (state->owner != GetCurrentThreadId()) { return kPermission; }
        if (--state->depth == 0) {
            state->owner = 0;
            state->changed.notify_one();
        }
        return 0;
    } catch (...) { return kMemory; }
}

int KT_SYSV MutexDestroy(std::uint64_t* slot) {
    if (!slot) { return kInvalid; }
    std::lock_guard registry_lock(registry.gate);
    if (*slot == 0) { return 0; } // untouched static initializer
    const auto found = registry.mutexes.find(*slot);
    if (found == registry.mutexes.end()) { return kInvalid; }
    // Keep ownership until the state lock is released; erasing the last shared
    // pointer while holding that state's mutex would destroy a locked mutex.
    const auto state = found->second;
    std::lock_guard state_lock(state->gate);
    if (state->owner || state->waiters || state->condition_users) { return kBusy; }
    state->destroyed = true;
    registry.mutexes.erase(found);
    *slot = 2; // destroyed, must not be mistaken for a fresh static initializer
    return 0;
}

template<typename Function> __declspec(noinline) int ConditionApi(const void* caller, Function&& function) {
    try { return function(); }
    catch (const std::bad_alloc&) { return kMemory; }
    catch (...) { UnsupportedApi("native_condition_host_failure", 0, caller); }
}
int ConditionAttributeInit(std::uint64_t* slot, const void* caller) {
    return ConditionApi(caller, [&] {
        if (!slot) { return kInvalid; }
        std::lock_guard lock(registry.gate);
        if (registry.condition_attributes.contains(*slot)) { return kBusy; }
        const auto token = NextToken(); registry.condition_attributes.emplace(token, 0); // private, CLOCK_REALTIME
        *slot = token; return 0;
    });
}
int ConditionAttributeDestroy(std::uint64_t* slot, const void* caller) {
    return ConditionApi(caller, [&] {
        if (!slot) { return kInvalid; }
        std::lock_guard lock(registry.gate);
        if (!registry.condition_attributes.erase(*slot)) { return kInvalid; }
        *slot = 0; return 0;
    });
}
int ConditionInit(std::uint64_t* slot, const std::uint64_t* attribute, const void* caller) {
    return ConditionApi(caller, [&] {
        if (!slot) { return kInvalid; }
        std::lock_guard lock(registry.gate);
        if (registry.conditions.contains(*slot)) { return kBusy; }
        unsigned clock = 0;
        if (attribute) {
            const auto found = registry.condition_attributes.find(*attribute);
            if (found == registry.condition_attributes.end()) { return kInvalid; }
            clock = found->second;
        }
        const auto token = NextToken(); registry.conditions.emplace(token, std::make_shared<ConditionState>(clock)); *slot = token;
        static std::atomic<unsigned> traced{};
        if (traced.fetch_add(1, std::memory_order_relaxed) < 16) {
            std::printf("{\"event\":\"native_condition_init\",\"token\":%llu,\"clock\":%u}\n", token, clock);
            TraceNativeApi("scePthreadCondInit", token, caller);
        }
        return 0;
    });
}
std::shared_ptr<ConditionState> Condition(std::uint64_t* slot) {
    if (!slot) { return {}; }
    std::lock_guard lock(registry.gate);
    if (*slot == 0) {
        const auto token = NextToken(); registry.conditions.emplace(token, std::make_shared<ConditionState>(0)); *slot = token;
    }
    const auto found = registry.conditions.find(*slot);
    return found == registry.conditions.end() ? nullptr : found->second;
}
int ConditionDestroy(std::uint64_t* slot, const void* caller) {
    return ConditionApi(caller, [&] {
        if (!slot) { return kInvalid; }
        std::lock_guard registry_lock(registry.gate);
        if (*slot == 0) { return 0; }
        const auto found = registry.conditions.find(*slot);
        if (found == registry.conditions.end()) { return kInvalid; }
        const auto condition = found->second;
        std::lock_guard state_lock(condition->gate);
        if (condition->active) { return kBusy; }
        condition->destroyed = true; registry.conditions.erase(found); *slot = 2; return 0;
    });
}
int NotifyCondition(std::uint64_t* slot, bool all, const void* caller) {
    return ConditionApi(caller, [&] {
        const auto condition = Condition(slot); if (!condition) { return kInvalid; }
        std::lock_guard lock(condition->gate);
        if (condition->destroyed) { return kInvalid; }
        unsigned notified = 0;
        while (!condition->queue.empty()) {
            auto* waiter = condition->queue.front(); condition->queue.pop_front();
            waiter->notified = true; waiter->ready.notify_one(); ++notified;
            if (!all) { break; }
        }
        // Dynamic mutex binding ends when the final queued wait is unblocked,
        // not when that wait finally reacquires its original mutex.
        if (condition->queue.empty()) { condition->bound_mutex.reset(); }
        static std::atomic<unsigned> traced{};
        if (traced.fetch_add(1, std::memory_order_relaxed) < 16) {
            std::printf("{\"event\":\"native_condition_notify\",\"broadcast\":%s,\"notified\":%u,\"active\":%u}\n", all ? "true" : "false", notified, condition->active);
        }
        return 0;
    });
}
// The optional steady deadline is solely a bounded host-test mechanism. No
// guest timed-wait/clock conversion is bound by this implementation.
__declspec(noinline) int WaitCondition(std::uint64_t* slot, std::uint64_t* mutex_slot,
        const std::chrono::steady_clock::time_point* deadline, const void* caller) {
    NativeHostOperationScope timing("scePthreadCondWait", "synchronization", 0, caller);
    bool released = false;
    try {
        if (!slot || !mutex_slot) { return kInvalid; }
        const auto condition = Condition(slot); if (!condition) { return kInvalid; }
        const auto mutex = State(mutex_slot);
        if (!condition || !mutex) { return kInvalid; }
        ConditionWaiter waiter;
        std::unique_lock condition_lock(condition->gate);
        if (condition->destroyed) { return kInvalid; }
        std::unique_lock mutex_lock(mutex->gate);
        if (mutex->destroyed) { return kInvalid; }
        const auto thread = GetCurrentThreadId();
        if (mutex->owner != thread) { return kPermission; }
        if (mutex->depth != 1) { return kUnsupported; }
        if (condition->bound_mutex && condition->bound_mutex != mutex) { return kInvalid; }
        if (condition->active == UINT_MAX || mutex->condition_users == UINT_MAX) { return kAgain; }
        // Allocate/construct everything before releasing guest ownership. The
        // condition lock keeps a concurrent notifier from losing this waiter.
        if (condition_queue_failure.exchange(false)) { throw std::bad_alloc(); }
        const auto queued = condition->queue.insert(condition->queue.end(), &waiter);
        condition->bound_mutex = mutex; ++condition->active; ++mutex->condition_users;
        const auto sequence = [] { static std::atomic<std::uint64_t> count{}; return count.fetch_add(1, std::memory_order_relaxed); }();
        if (sequence < 8) { TraceNativeApi("scePthreadCondWait_begin", sequence, caller); }
        released = true; mutex->owner = 0; mutex->depth = 0; mutex->changed.notify_one(); mutex_lock.unlock();
        bool notified = true;
        if (deadline) { notified = waiter.ready.wait_until(condition_lock, *deadline, [&] { return waiter.notified; }); }
        else { waiter.ready.wait(condition_lock, [&] { return waiter.notified; }); }
        if (!notified) {
            condition->queue.erase(queued);
            if (condition->queue.empty()) { condition->bound_mutex.reset(); }
        }
        condition_lock.unlock();
        // condition_users prevents destruction while asleep or waiting to
        // reacquire. Registry locks are never held across either wait.
        mutex_lock.lock();
        mutex->changed.wait(mutex_lock, [&] { return !mutex->owner || mutex->destroyed; });
        if (mutex->destroyed || !mutex->condition_users) { UnsupportedApi("native_condition_mutex_lifetime_failure", 0, caller); }
        mutex->owner = thread; mutex->depth = 1; --mutex->condition_users; mutex_lock.unlock();
        condition_lock.lock(); --condition->active; condition_lock.unlock();
        released = false;
        if (sequence < 8) { TraceNativeApi("scePthreadCondWait_end", sequence, caller); }
        return notified ? 0 : kTimeout;
    } catch (const std::bad_alloc&) {
        if (released) { UnsupportedApi("native_condition_failure_after_mutex_release", 0, caller); }
        return kMemory;
    } catch (...) { UnsupportedApi("native_condition_wait_host_failure", 0, caller); }
}
int KT_SYSV KernelConditionAttributeInit(std::uint64_t* slot) { return ConditionAttributeInit(slot, __builtin_return_address(0)); }
int KT_SYSV KernelConditionAttributeDestroy(std::uint64_t* slot) { return ConditionAttributeDestroy(slot, __builtin_return_address(0)); }
int KT_SYSV KernelConditionInit(std::uint64_t* slot, const std::uint64_t* attr, const char* /*name*/) { return ConditionInit(slot, attr, __builtin_return_address(0)); }
int KT_SYSV KernelConditionDestroy(std::uint64_t* slot) { return ConditionDestroy(slot, __builtin_return_address(0)); }
int KT_SYSV KernelConditionSignal(std::uint64_t* slot) { return NotifyCondition(slot, false, __builtin_return_address(0)); }
int KT_SYSV KernelConditionBroadcast(std::uint64_t* slot) { return NotifyCondition(slot, true, __builtin_return_address(0)); }
int KT_SYSV KernelConditionWait(std::uint64_t* slot, std::uint64_t* mutex) { return WaitCondition(slot, mutex, nullptr, __builtin_return_address(0)); }
} // namespace

std::uint64_t ResolveMutexImport(std::string_view identifier) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    struct Binding { std::string_view hash; std::uint64_t address; };
    const Binding bindings[]{
        {"F8bUHwAG284", reinterpret_cast<std::uint64_t>(&AttributeInit)},
        {"iMp8QpE+XO4", reinterpret_cast<std::uint64_t>(&AttributeSetType)},
        {"smWEktiyyG0", reinterpret_cast<std::uint64_t>(&AttributeDestroy)},
        {"cmo1RIYva9o", reinterpret_cast<std::uint64_t>(&MutexInit)},
        {"2Of0f+3mhhE", reinterpret_cast<std::uint64_t>(&MutexDestroy)},
        {"9UK1vLZQft4", reinterpret_cast<std::uint64_t>(&MutexLock)},
        {"upoVrzMHFeE", reinterpret_cast<std::uint64_t>(&MutexTryLock)},
        {"tn3VlD0hG60", reinterpret_cast<std::uint64_t>(&MutexUnlock)},
        {"m5-2bsNfv7s", reinterpret_cast<std::uint64_t>(&KernelConditionAttributeInit)},
        {"waPcxYiR3WA", reinterpret_cast<std::uint64_t>(&KernelConditionAttributeDestroy)},
        {"2Tb92quprl0", reinterpret_cast<std::uint64_t>(&KernelConditionInit)},
        {"g+PZd2hiacg", reinterpret_cast<std::uint64_t>(&KernelConditionDestroy)},
        {"kDh-NfxgMtE", reinterpret_cast<std::uint64_t>(&KernelConditionSignal)},
        {"JGgj7Uvrl+A", reinterpret_cast<std::uint64_t>(&KernelConditionBroadcast)},
        {"WKAXJ4XBPQ4", reinterpret_cast<std::uint64_t>(&KernelConditionWait)},
    };
    for (const auto& binding : bindings) {
        if (hash == binding.hash) { return binding.address; }
    }
    return 0;
}

bool MutexSelfTest() {
    bool passed = true;
    const auto check = [&](const char* name, bool result) {
        std::printf("{\"event\":\"mutex_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n",
                    name, result ? "true" : "false");
        passed &= result;
    };
    std::uint64_t attribute{}, recursive{};
    check("attribute_validation", AttributeInit(nullptr) == kInvalid && AttributeInit(&attribute) == 0 &&
          AttributeSetType(&attribute, 0) == kInvalid && AttributeSetType(&attribute, 4) == kUnsupported);
    check("recursive_init", AttributeSetType(&attribute, 2) == 0 &&
          MutexInit(&recursive, &attribute, nullptr) == 0 && AttributeDestroy(&attribute) == 0 &&
          AttributeDestroy(&attribute) == kInvalid);
    check("recursive_balance", MutexLock(&recursive) == 0 && MutexTryLock(&recursive) == 0 &&
          MutexDestroy(&recursive) == kBusy && MutexUnlock(&recursive) == 0 &&
          MutexDestroy(&recursive) == kBusy && MutexUnlock(&recursive) == 0 &&
          MutexUnlock(&recursive) == kPermission);
    check("destroyed_handle", MutexDestroy(&recursive) == 0 && MutexLock(&recursive) == kInvalid &&
          MutexDestroy(&recursive) == kInvalid);
    std::uint64_t checked{};
    check("static_errorcheck", MutexLock(&checked) == 0 && MutexLock(&checked) == kDeadlock &&
          MutexTryLock(&checked) == kBusy);
    bool wrong_owner = false;
    std::thread intruder([&] { wrong_owner = MutexUnlock(&checked) == kPermission &&
                                         MutexTryLock(&checked) == kBusy; });
    intruder.join();
    check("ownership", wrong_owner && MutexUnlock(&checked) == 0 && MutexDestroy(&checked) == 0);
    std::uint64_t shared{};
    std::uint64_t count = 0;
    std::atomic<bool> worker_failed{};
    std::array<std::thread, 4> workers;
    for (auto& worker : workers) {
        worker = std::thread([&] {
            for (unsigned i = 0; i < 1000; ++i) {
                if (MutexLock(&shared)) { worker_failed = true; return; }
                ++count;
                if (MutexUnlock(&shared)) { worker_failed = true; return; }
            }
        });
    }
    for (auto& worker : workers) { worker.join(); }
    check("contention", !worker_failed && count == 4000 && MutexDestroy(&shared) == 0);
    check("null_handles", MutexLock(nullptr) == kInvalid && MutexUnlock(nullptr) == kInvalid &&
          MutexDestroy(nullptr) == kInvalid);
    std::printf("{\"event\":\"mutex_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}

bool ConditionSelfTest() {
    using namespace std::chrono_literals;
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        std::printf("{\"event\":\"condition_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false"); passed &= value;
    };
    const auto wait = [](std::uint64_t* condition, std::uint64_t* mutex, std::chrono::milliseconds delay = 2000ms) {
        const auto deadline = std::chrono::steady_clock::now() + delay; return WaitCondition(condition, mutex, &deadline, nullptr);
    };
    const auto await = [](const auto& predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!predicate() && std::chrono::steady_clock::now() < deadline) { Sleep(1); }
        return predicate();
    };
    std::uint64_t attribute{}, condition{}, mutex{}, invalid_attribute = 999;
    check("attribute_init_null_and_duplicate", ConditionAttributeInit(nullptr, nullptr) == kInvalid && ConditionAttributeInit(&attribute, nullptr) == 0 &&
          ConditionAttributeInit(&attribute, nullptr) == kBusy);
    check("invalid_attributes_do_not_publish_condition", ConditionInit(&condition, &invalid_attribute, nullptr) == kInvalid && condition == 0 && ConditionInit(nullptr, nullptr, nullptr) == kInvalid);
    check("initialized_attribute_is_copied_not_borrowed", ConditionInit(&condition, &attribute, nullptr) == 0 && ConditionAttributeDestroy(&attribute, nullptr) == 0 &&
          ConditionAttributeDestroy(&attribute, nullptr) == kInvalid && Condition(&condition)->clock == 0 && ConditionInit(&condition, nullptr, nullptr) == kBusy);
    check("wait_requires_ownership_without_unlocking_someone_else", MutexInit(&mutex, nullptr, nullptr) == 0 && wait(&condition, &mutex, 0ms) == kPermission &&
          Condition(&condition)->active == 0);
    check("signal_without_waiter_is_not_stored_as_credit", NotifyCondition(&condition, false, nullptr) == 0 && MutexLock(&mutex) == 0 &&
          wait(&condition, &mutex, 20ms) == kTimeout && MutexLock(&mutex) == kDeadlock && MutexUnlock(&mutex) == 0);
    condition_queue_failure = true;
    check("allocation_failure_before_unlock_preserves_mutex_and_queue", MutexLock(&mutex) == 0 && wait(&condition, &mutex) == kMemory &&
          MutexLock(&mutex) == kDeadlock && Condition(&condition)->queue.empty() && Condition(&condition)->active == 0 && MutexUnlock(&mutex) == 0);
    condition_queue_failure = false;
    const auto stale = condition;
    check("destroyed_handles_do_not_reinitialize_or_notify_new_object", ConditionDestroy(&condition, nullptr) == 0 && NotifyCondition(&condition, false, nullptr) == kInvalid &&
          ConditionInit(&condition, nullptr, nullptr) == 0 && condition != stale);
    auto stale_slot = stale;
    check("stale_copied_token_and_attribute_kind_rejected", NotifyCondition(&stale_slot, false, nullptr) == kInvalid &&
          ConditionAttributeDestroy(&condition, nullptr) == kInvalid && ConditionDestroy(&condition, nullptr) == 0 && MutexDestroy(&mutex) == 0);
    {
        std::uint64_t attr{}, recursive{}, cv{};
        const bool created = AttributeInit(&attr) == 0 && AttributeSetType(&attr, 2) == 0 && MutexInit(&recursive, &attr, nullptr) == 0 &&
                             AttributeDestroy(&attr) == 0 && ConditionInit(&cv, nullptr, nullptr) == 0;
        check("recursive_depth_above_one_is_explicitly_unsupported_and_preserved", created && MutexLock(&recursive) == 0 && MutexLock(&recursive) == 0 &&
              wait(&cv, &recursive) == kUnsupported && State(&recursive)->depth == 2 && MutexUnlock(&recursive) == 0 && MutexUnlock(&recursive) == 0 &&
              ConditionDestroy(&cv, nullptr) == 0 && MutexDestroy(&recursive) == 0);
    }
    {
        std::uint64_t cv{}, mx{}; MutexInit(&mx, nullptr, nullptr); ConditionInit(&cv, nullptr, nullptr);
        std::atomic<bool> ready{}, returned{}, correct{true};
        std::thread worker([&] {
            correct = MutexLock(&mx) == 0; ready = true;
            const auto result = wait(&cv, &mx);
            correct = correct && result == 0 && MutexLock(&mx) == kDeadlock && MutexUnlock(&mx) == 0;
            returned = true;
        });
        const bool started = await([&] { return ready.load(); });
        MutexLock(&mx); MutexUnlock(&mx); // after the worker's atomic enqueue/unlock
        bool unowned{};
        { const auto snapshot = State(&mx); std::lock_guard lock(snapshot->gate); unowned = snapshot->owner == 0; }
        check("sleeping_condition_wait_pins_unowned_mutex_and_condition", started && !returned && unowned &&
              MutexDestroy(&mx) == kBusy && ConditionDestroy(&cv, nullptr) == kBusy);
        MutexLock(&mx); NotifyCondition(&cv, false, nullptr); Sleep(10);
        check("notification_cannot_return_until_original_mutex_is_reacquired", !returned && ConditionDestroy(&cv, nullptr) == kBusy);
        MutexUnlock(&mx); worker.join();
        check("signalled_wait_returns_owned_and_both_objects_can_be_destroyed", correct && returned && ConditionDestroy(&cv, nullptr) == 0 && MutexDestroy(&mx) == 0);
    }
    {
        std::uint64_t cv{}, mx{}; MutexInit(&mx, nullptr, nullptr); ConditionInit(&cv, nullptr, nullptr);
        std::atomic<unsigned> ready{}, completed{}, errors{};
        std::array<std::thread, 4> workers;
        for (auto& worker : workers) {
            worker = std::thread([&] {
                if (MutexLock(&mx)) { ++errors; return; }
                ++ready; const auto result = wait(&cv, &mx);
                if (result || MutexUnlock(&mx)) { ++errors; } ++completed;
            });
        }
        const bool started = await([&] { return ready == 4; });
        MutexLock(&mx); NotifyCondition(&cv, false, nullptr);
        auto state = Condition(&cv);
        bool exactly_one{};
        { std::lock_guard lock(state->gate); exactly_one = state->queue.size() == 3; }
        MutexUnlock(&mx);
        const bool one_returned = await([&] { return completed >= 1; });
        check("signal_unblocks_exactly_one_current_waiter", started && exactly_one && one_returned && completed == 1 && errors == 0);
        MutexLock(&mx); NotifyCondition(&cv, true, nullptr); MutexUnlock(&mx);
        for (auto& worker : workers) { worker.join(); }
        check("broadcast_unblocks_remaining_waiters_and_no_objects_stay_busy", completed == 4 && errors == 0 &&
              ConditionDestroy(&cv, nullptr) == 0 && MutexDestroy(&mx) == 0);
    }
    {
        std::uint64_t cv{}, first{}, second{}; MutexInit(&first, nullptr, nullptr); MutexInit(&second, nullptr, nullptr); ConditionInit(&cv, nullptr, nullptr);
        std::atomic<bool> ready{}, correct{};
        std::thread worker([&] {
            MutexLock(&first); ready = true;
            const auto result = wait(&cv, &first); correct = result == 0 && MutexUnlock(&first) == 0;
        });
        await([&] { return ready.load(); }); MutexLock(&first); MutexLock(&second);
        const bool mismatch = wait(&cv, &second) == kInvalid && MutexLock(&second) == kDeadlock;
        NotifyCondition(&cv, false, nullptr); // old waiter is blocked reacquiring first
        const bool rebound = wait(&cv, &second, 20ms) == kTimeout && MutexLock(&second) == kDeadlock;
        MutexUnlock(&second); MutexUnlock(&first); worker.join();
        check("dynamic_binding_rejects_other_mutex_until_last_waiter_is_unblocked", mismatch && rebound && correct &&
              ConditionDestroy(&cv, nullptr) == 0 && MutexDestroy(&first) == 0 && MutexDestroy(&second) == 0);
    }
    {
        std::uint64_t cv{}, mx{}; MutexInit(&mx, nullptr, nullptr); ConditionInit(&cv, nullptr, nullptr);
        int turn = 0; unsigned exchanged = 0; std::atomic<bool> failed{};
        std::thread worker([&] {
            if (MutexLock(&mx)) { failed = true; return; }
            for (unsigned i = 0; i < 300 && !failed; ++i) {
                while (turn != 1 && !failed) { if (wait(&cv, &mx)) { failed = true; } }
                if (failed) { break; }
                ++exchanged; turn = 0; NotifyCondition(&cv, false, nullptr);
            }
            NotifyCondition(&cv, true, nullptr); if (MutexUnlock(&mx)) { failed = true; }
        });
        if (MutexLock(&mx)) { failed = true; }
        else {
            for (unsigned i = 0; i < 300 && !failed; ++i) {
                while (turn != 0 && !failed) { if (wait(&cv, &mx)) { failed = true; } }
                if (failed) { break; }
                turn = 1; NotifyCondition(&cv, false, nullptr);
            }
            NotifyCondition(&cv, true, nullptr); if (MutexUnlock(&mx)) { failed = true; }
        }
        worker.join();
        check("three_hundred_cross_thread_handoffs_do_not_lose_notifications", !failed && exchanged == 300 &&
              ConditionDestroy(&cv, nullptr) == 0 && MutexDestroy(&mx) == 0);
    }
    {
        std::uint64_t cv{}; std::atomic<unsigned> errors{}; std::array<std::thread, 4> workers;
        for (auto& worker : workers) { worker = std::thread([&] { if (NotifyCondition(&cv, false, nullptr)) { ++errors; } }); }
        for (auto& worker : workers) { worker.join(); }
        check("concurrent_static_initialization_publishes_one_condition", errors == 0 && cv > 2 && ConditionDestroy(&cv, nullptr) == 0);
    }
    std::uint64_t empty{}, invalid_mutex = 999;
    check("null_destroyed_and_foreign_objects_do_not_wait", ConditionDestroy(nullptr, nullptr) == kInvalid && ConditionDestroy(&empty, nullptr) == 0 &&
          NotifyCondition(nullptr, false, nullptr) == kInvalid && wait(nullptr, &invalid_mutex) == kInvalid && wait(&empty, nullptr) == kInvalid);
    {
        std::lock_guard lock(registry.gate);
        check("all_condition_and_mutex_objects_released_after_tests", registry.conditions.empty() && registry.condition_attributes.empty() && registry.mutexes.empty() && registry.attributes.empty());
    }
    std::printf("{\"event\":\"condition_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
