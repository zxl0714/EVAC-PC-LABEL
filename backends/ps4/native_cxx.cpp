#include "native_cxx.h"
#include "native_diagnostics.h"

#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
struct Guard {
    std::condition_variable changed;
    DWORD owner{};
    unsigned waiters{};
};
struct Registry {
    std::mutex mutex;
    std::map<std::uint64_t*, Guard> guards;
};
Registry& Guards() { static Registry registry; return registry; }

bool Initialized(std::uint64_t* address) {
    return std::atomic_ref<unsigned char>(*reinterpret_cast<unsigned char*>(address)).load(std::memory_order_acquire) != 0;
}
void CheckAddress(std::uint64_t* address, const void* caller) {
    if (!address || reinterpret_cast<std::uintptr_t>(address) % alignof(std::uint64_t)) {
        UnsupportedApi("invalid_cxa_guard_address", reinterpret_cast<std::uintptr_t>(address), caller);
    }
}

int KT_SYSV Acquire(std::uint64_t* address) {
    const auto caller = __builtin_return_address(0);
    CheckAddress(address, caller);
    if (Initialized(address)) { return 0; }
    try {
        auto& registry = Guards();
        std::unique_lock lock(registry.mutex);
        if (Initialized(address)) { return 0; }
        auto iterator = registry.guards.find(address);
        if (iterator == registry.guards.end()) {
            if (registry.guards.size() >= 65536) { UnsupportedApi("cxa_guard_registry_limit", registry.guards.size(), caller); }
            iterator = registry.guards.try_emplace(address).first;
        }
        auto& guard = iterator->second;
        const auto thread = GetCurrentThreadId();
        while (guard.owner) {
            if (guard.owner == thread) { UnsupportedApi("recursive_cxa_guard_initialization", reinterpret_cast<std::uintptr_t>(address), caller); }
            ++guard.waiters;
            guard.changed.wait(lock, [&] { return !guard.owner; });
            --guard.waiters;
            if (Initialized(address)) { return 0; }
        }
        guard.owner = thread;
        TraceNativeApi("__cxa_guard_acquire", reinterpret_cast<std::uintptr_t>(address), caller);
        return 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Native C++ guard error: %s\n", error.what());
        // Returning zero would falsely claim a completed constructor. Host
        // exceptions cannot unwind through the original ELF stack either.
        UnsupportedApi("native_cxa_guard_host", 0, caller);
    }
}

void Finish(std::uint64_t* address, bool completed, const void* caller) {
    CheckAddress(address, caller);
    try {
        auto& registry = Guards();
        std::lock_guard lock(registry.mutex);
        const auto iterator = registry.guards.find(address);
        if (iterator == registry.guards.end() || iterator->second.owner != GetCurrentThreadId()) {
            UnsupportedApi("cxa_guard_not_owned", reinterpret_cast<std::uintptr_t>(address), caller);
        }
        auto& guard = iterator->second;
        if (completed) {
            std::atomic_ref<unsigned char>(*reinterpret_cast<unsigned char*>(address)).store(1, std::memory_order_release);
        }
        guard.owner = 0;
        guard.changed.notify_all();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Native C++ guard finish error: %s\n", error.what());
        UnsupportedApi("native_cxa_guard_host", 0, caller);
    }
}
void KT_SYSV Release(std::uint64_t* address) { Finish(address, true, __builtin_return_address(0)); }
void KT_SYSV Abort(std::uint64_t* address) { Finish(address, false, __builtin_return_address(0)); }
} // namespace

std::uint64_t ResolveCxxGuardImport(std::string_view identifier) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "3GPpjQdAMTw") { return reinterpret_cast<std::uint64_t>(&Acquire); }
    if (hash == "9rAeANT2tyE") { return reinterpret_cast<std::uint64_t>(&Release); }
    if (hash == "2emaaluWzUw") { return reinterpret_cast<std::uint64_t>(&Abort); }
    return 0;
}

bool CxxGuardSelfTest() {
    bool passed = true;
    auto check = [&](const char* name, bool result) {
        passed &= result;
        std::printf("{\"event\":\"cxx_guard_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    std::uint64_t once = 0xaabbccddeeff0000ull;
    const bool first = Acquire(&once) == 1 && !Initialized(&once);
    Release(&once);
    check("release_marks_only_initialization_byte", first && Acquire(&once) == 0 && once == 0xaabbccddeeff0001ull);
    std::uint64_t aborted{};
    const bool abort_first = Acquire(&aborted) == 1;
    Abort(&aborted);
    const bool retry = !Initialized(&aborted) && Acquire(&aborted) == 1;
    Release(&aborted);
    check("abort_allows_retry", abort_first && retry && Acquire(&aborted) == 0);
    std::uint64_t independent1{}, independent2{};
    const bool independent = Acquire(&independent1) == 1 && Acquire(&independent2) == 1;
    Release(&independent2); Release(&independent1);
    check("different_guards_do_not_block_each_other", independent && Initialized(&independent1) && Initialized(&independent2));

    std::uint64_t concurrent{};
    int payload{};
    std::atomic<bool> start{}, correct{true};
    std::atomic<unsigned> constructors{};
    std::vector<std::thread> threads;
    for (unsigned index = 0; index < 6; ++index) {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) { Sleep(0); }
            for (unsigned iteration = 0; iteration < 200; ++iteration) {
                if (Acquire(&concurrent)) { ++constructors; Sleep(10); payload = 42; Release(&concurrent); }
                if (payload != 42) { correct.store(false); }
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) { thread.join(); }
    check("six_threads_construct_once_and_observe_payload", correct.load() && constructors.load() == 1 && payload == 42);

    std::uint64_t retry_waiter{};
    Acquire(&retry_waiter);
    std::atomic<bool> acquired{};
    std::thread waiter([&] { acquired.store(Acquire(&retry_waiter) == 1); Release(&retry_waiter); });
    bool waiting = false;
    const auto deadline = GetTickCount64() + 1000;
    while (GetTickCount64() < deadline) {
        { auto& registry = Guards(); std::lock_guard lock(registry.mutex); waiting = registry.guards.at(&retry_waiter).waiters != 0; }
        if (waiting) { break; }
        Sleep(0);
    }
    Abort(&retry_waiter);
    waiter.join();
    check("abort_wakes_waiting_initializer", waiting && acquired.load() && Initialized(&retry_waiter));
    std::printf("{\"event\":\"cxx_guard_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
