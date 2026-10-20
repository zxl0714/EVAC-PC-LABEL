#include "native_threads.h"
#include "native_diagnostics.h"
#include <windows.h>
#include <process.h>
#include <atomic>
#include <array>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
using Token = std::uint64_t;
using Entry = void*(KT_SYSV*)(void*);
constexpr int Error(unsigned code) { return static_cast<int>(0x80020000u | code); }
constexpr int kSearch = Error(3), kDeadlock = Error(11), kMemory = Error(12), kInvalid = Error(22), kAgain = Error(35);
constexpr int kUnsupported = Error(45);
constexpr std::size_t kDefaultStack = 1024 * 1024, kMinimumStack = 16 * 1024;
// Zero is our internal unrestricted default, not a valid guest Setaffinity
// argument. Explicit masks describe six virtual application CPUs, never raw
// Windows CPU numbers. Retain/validate guest metadata, but let the host
// scheduler place native threads within their inherited eligible set.
struct Attribute { std::size_t stack = kDefaultStack; int inherit = 4, priority = 700; Token affinity{}; };
struct Scheduling { int priority; };
static_assert(sizeof(Scheduling) == 4);
struct Thread {
    HANDLE native{}, start{};
    Token token{}, waiting_for{};
    unsigned native_id{};
    std::size_t stack_bytes{};
    int inheritance = 4, guest_priority = 700, native_priority = THREAD_PRIORITY_NORMAL;
    Token guest_affinity{}, native_affinity{};
    Entry entry{};
    void* argument{};
    std::atomic<void*> result{};
    std::atomic<bool> cancelled{};
    bool joining{};
    ~Thread() { if (native) { CloseHandle(native); } if (start) { CloseHandle(start); } }
};
struct Registry {
    std::mutex gate;
    Token next = 0x5054000000000001ull;
    unsigned pending = 0;
    std::map<Token, Attribute> attributes;
    std::map<Token, std::shared_ptr<Thread>> threads;
};
Registry registry;
thread_local Token current_thread{};
// Scheduling priority is immutable after Create's attribute snapshot. A future
// live-priority setter must update this contract as well as the registry.
thread_local int current_guest_priority = 700;

template<typename Function> __declspec(noinline) int Api(const void* caller, Function&& function) {
    try { return function(); }
    catch (const std::bad_alloc&) { return kMemory; }
    catch (...) { UnsupportedApi("native_thread_host_exception", 0, caller); }
}
Token Next() {
    if (registry.next == (std::numeric_limits<Token>::max)()) { throw std::bad_alloc(); }
    return registry.next++;
}
int Init(Token* output) {
    return Api(nullptr, [&] {
        if (!output) { return kInvalid; }
        std::lock_guard lock(registry.gate);
        if (registry.attributes.size() >= 4096) { return kMemory; }
        const auto token = Next(); registry.attributes.emplace(token, Attribute{}); *output = token; return 0;
    });
}
int Stack(Token* slot, std::size_t bytes) {
    return Api(nullptr, [&] {
        if (!slot || bytes < kMinimumStack) { return kInvalid; }
        std::lock_guard lock(registry.gate);
        const auto found = registry.attributes.find(*slot);
        if (found == registry.attributes.end()) { return kInvalid; }
        found->second.stack = bytes; return 0;
    });
}
int Inherit(Token* slot, int mode) {
    return Api(nullptr, [&] {
        if (!slot) { return kInvalid; }
        std::lock_guard lock(registry.gate);
        const auto found = registry.attributes.find(*slot); if (found == registry.attributes.end()) { return kInvalid; }
        if (mode != 0 && mode != 4) { return kUnsupported; }
        found->second.inherit = mode; return 0;
    });
}
int Priority(Token* slot, const Scheduling* scheduling, const void* caller) {
    return Api(caller, [&] {
        if (!slot) { return kInvalid; }
        std::lock_guard lock(registry.gate);
        const auto found = registry.attributes.find(*slot); if (found == registry.attributes.end()) { return kInvalid; }
        if (!scheduling) { return kUnsupported; }
        if (scheduling->priority < 256 || scheduling->priority > 767) { return kUnsupported; }
        found->second.priority = scheduling->priority;
        TraceNativeApi("scePthreadAttrSetschedparam", static_cast<unsigned>(scheduling->priority), caller); return 0;
    });
}
int Affinity(Token* slot, Token mask, const void* caller) {
    return Api(caller, [&] {
        if (!slot) { return kInvalid; }
        std::lock_guard lock(registry.gate);
        const auto found = registry.attributes.find(*slot); if (found == registry.attributes.end()) { return kInvalid; }
        if (!mask) { return kInvalid; }
        if (mask & ~Token{0x3f}) { return kUnsupported; }
        TraceNativeApi("scePthreadAttrSetaffinity", mask, caller);
        found->second.affinity = mask; return 0;
    });
}
Token MapAffinity(Token guest, Token available) {
    std::array<Token, 64> processors{}; unsigned count{};
    for (unsigned bit = 0; bit < 64; ++bit) { if (available & (Token{1} << bit)) { processors[count++] = Token{1} << bit; } }
    if (!count || !guest || (guest & ~Token{0x3f})) { return 0; }
    Token result{};
    for (unsigned bit = 0; bit < 6; ++bit) { if (guest & (Token{1} << bit)) { result |= processors[bit % count]; } }
    return result;
}
bool ApplyAffinity(Thread& thread) {
    if (!thread.guest_affinity) { return true; }
    // Single-group backend only. Do not silently narrow a multi-group process
    // or assume a process mask from one group describes a different group.
    if (GetActiveProcessorGroupCount() != 1) { return false; }
    DWORD_PTR available{}, system{};
    if (!GetProcessAffinityMask(GetCurrentProcess(), &available, &system) || !available) { return false; }
    // A console virtual CPU index is not a Windows logical processor index.
    // Hard-pinning six task workers to six host LPs amplified late wakeups and
    // held the entire guest batch behind its last reserved credit. Do not
    // rebind or broaden the thread: keep the host's existing eligible set.
    // The retired hard map remains available only to the offline A/B fixture.
    GROUP_AFFINITY actual{};
    if (!GetThreadGroupAffinity(thread.native, &actual) || actual.Group != 0 ||
        !actual.Mask || (actual.Mask & ~available)) { return false; }
    thread.native_affinity = actual.Mask;
    return true;
}
int WindowsPriority(int priority) {
    // Portable development profile, not PS4 FIFO/RR emulation. Collapse the
    // console range into safe relative Windows tiers around default 700.
    // Never select TIME_CRITICAL, IDLE, or change the process priority class.
    if (priority < 512) { return THREAD_PRIORITY_HIGHEST; }
    if (priority < 700) { return THREAD_PRIORITY_ABOVE_NORMAL; }
    if (priority == 700) { return THREAD_PRIORITY_NORMAL; }
    return THREAD_PRIORITY_BELOW_NORMAL;
}
int DestroyAttribute(Token* slot) {
    return Api(nullptr, [&] {
        if (!slot) { return kInvalid; }
        std::lock_guard lock(registry.gate);
        if (!registry.attributes.erase(*slot)) { return kInvalid; }
        *slot = 0; return 0;
    });
}
unsigned __stdcall Run(void* raw) noexcept {
    std::unique_ptr<std::shared_ptr<Thread>> payload(static_cast<std::shared_ptr<Thread>*>(raw));
    auto thread = std::move(*payload); payload.reset();
    if (WaitForSingleObject(thread->start, INFINITE) != WAIT_OBJECT_0) {
        UnsupportedApi("native_thread_start_wait", GetLastError(), nullptr);
    }
    if (thread->cancelled.load(std::memory_order_acquire)) { return 0; }
    current_thread = thread->token;
    current_guest_priority = thread->guest_priority;
    ULONG_PTR low{}, high{}; MEMORY_BASIC_INFORMATION stack{};
    GetCurrentThreadStackLimits(&low, &high);
    if (VirtualQuery(&payload, &stack, sizeof(stack)) != sizeof(stack)) { UnsupportedApi("native_thread_stack_query", GetLastError(), nullptr); }
    std::printf("{\"event\":\"native_thread_start\",\"token\":%llu,\"native_id\":%u,\"requested_stack_bytes\":%zu,\"reserved_stack_bytes\":%llu,\"inheritance\":%d,\"guest_priority\":%d,\"native_priority\":%d,\"guest_affinity\":%llu,\"native_affinity\":%llu,\"affinity_policy\":\"host_inherited\"}\n",
                thread->token, static_cast<unsigned>(GetCurrentThreadId()), thread->stack_bytes,
                static_cast<unsigned long long>(high - reinterpret_cast<std::uintptr_t>(stack.AllocationBase)), thread->inheritance, thread->guest_priority, GetThreadPriority(GetCurrentThread()), thread->guest_affinity, thread->native_affinity);
    // Clang knows this pointer uses SysV, and preserves the Windows nonvolatile
    // registers around it. Only normal return is implemented; pthread_exit,
    // ELF TLS, cancellation and original C++ unwinding are separate contracts.
    thread->result.store(thread->entry(thread->argument), std::memory_order_release);
    std::printf("{\"event\":\"native_thread_return\",\"token\":%llu}\n", thread->token);
    current_thread = 0;
    current_guest_priority = 700;
    return 0;
}
struct Pending {
    bool active = false;
    ~Pending() { if (active) { std::lock_guard lock(registry.gate); --registry.pending; } }
};
struct StartTransaction {
    std::shared_ptr<Thread> thread; bool committed = false;
    ~StartTransaction() {
        if (committed || !thread || !thread->native) { return; }
        // A failed publication may only wake the host trampoline's cancel
        // path. Never terminate a thread or run an unpublished guest entry.
        thread->cancelled.store(true, std::memory_order_release);
        if (!SetEvent(thread->start) || WaitForSingleObject(thread->native, 5000) != WAIT_OBJECT_0) {
            UnsupportedApi("native_thread_creation_rollback", GetLastError(), nullptr);
        }
    }
};
int Create(Token* output, const Token* attribute, Entry entry, void* argument, const char* name,
           const void* caller, bool fixture = false, bool fail_commit = false, bool fail_priority = false, bool fail_affinity = false) {
    return Api(caller, [&] {
        if (!output || !entry || (!fixture && !NativeExecutableAddress(reinterpret_cast<std::uintptr_t>(entry)))) { return kInvalid; }
        wchar_t description[32]{};
        if (name) {
            int length = 0;
            while (length < 32 && name[length]) { ++length; }
            if (length == 32 || (length && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, length, description, 31))) { return kInvalid; }
        }
        auto thread = std::make_shared<Thread>();
        thread->entry = entry; thread->argument = argument; thread->stack_bytes = kDefaultStack;
        Pending pending;
        {
            std::lock_guard lock(registry.gate);
            if (attribute) {
                const auto found = registry.attributes.find(*attribute);
                if (found == registry.attributes.end()) { return kInvalid; }
                thread->stack_bytes = found->second.stack; thread->inheritance = found->second.inherit; thread->guest_priority = found->second.priority;
                thread->guest_affinity = found->second.affinity;
            }
            if (thread->inheritance == 4) {
                const auto parent = registry.threads.find(current_thread);
                thread->guest_priority = parent == registry.threads.end() ? 700 : parent->second->guest_priority;
            }
            if (thread->stack_bytes > UINT32_MAX || registry.threads.size() + registry.pending >= 1024) { return kAgain; }
            thread->token = Next(); ++registry.pending; pending.active = true;
        }
        if (thread->inheritance == 4) {
            thread->native_priority = GetThreadPriority(GetCurrentThread());
            if (thread->native_priority == THREAD_PRIORITY_ERROR_RETURN) { return kAgain; }
        }
        else { thread->native_priority = WindowsPriority(thread->guest_priority); }
        thread->start = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!thread->start) { return kAgain; }
        auto payload = std::make_unique<std::shared_ptr<Thread>>(thread);
        thread->native = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, static_cast<unsigned>(thread->stack_bytes), Run,
                                  payload.get(), STACK_SIZE_PARAM_IS_A_RESERVATION, &thread->native_id));
        if (!thread->native) { return kAgain; }
        (void)payload.release();
        StartTransaction transaction{thread};
        if (fail_priority || !SetThreadPriority(thread->native, thread->native_priority) || GetThreadPriority(thread->native) != thread->native_priority) { return kAgain; }
        if (fail_affinity || !ApplyAffinity(*thread)) { return kAgain; }
        if (description[0] && FAILED(SetThreadDescription(thread->native, description))) { return kAgain; }
        {
            std::lock_guard lock(registry.gate);
            if (fail_commit) { throw std::bad_alloc(); }
            registry.threads.emplace(thread->token, thread);
            *output = thread->token;
        }
        if (!SetEvent(thread->start)) { UnsupportedApi("native_thread_publish_signal", GetLastError(), caller); }
        transaction.committed = true;
        std::printf("{\"event\":\"native_thread_create\",\"token\":%llu,\"native_id\":%u,\"requested_stack_bytes\":%zu}\n",
                    thread->token, thread->native_id, thread->stack_bytes);
        return 0;
    });
}
int Join(Token token, void** output, const void* caller) {
    return Api(caller, [&] {
        std::shared_ptr<Thread> target, self;
        {
            std::lock_guard lock(registry.gate);
            const auto found = registry.threads.find(token);
            if (found == registry.threads.end()) { return kSearch; }
            if (token == current_thread) { return kDeadlock; }
            if (found->second->joining) { return kInvalid; }
            if (const auto current = registry.threads.find(current_thread); current != registry.threads.end()) { self = current->second; }
            auto next = token;
            for (std::size_t depth = 0; next && depth <= registry.threads.size(); ++depth) {
                if (next == current_thread) { return kDeadlock; }
                const auto link = registry.threads.find(next);
                next = link == registry.threads.end() ? 0 : link->second->waiting_for;
            }
            target = found->second; target->joining = true;
            if (self) { self->waiting_for = token; }
        }
        const auto wait = WaitForSingleObject(target->native, INFINITE);
        {
            std::lock_guard lock(registry.gate);
            if (self) { self->waiting_for = 0; }
            if (wait == WAIT_OBJECT_0) { registry.threads.erase(token); }
            else { target->joining = false; }
        }
        if (wait != WAIT_OBJECT_0) { UnsupportedApi("native_thread_join_wait", GetLastError(), caller); }
        if (output) { *output = target->result.load(std::memory_order_acquire); }
        std::printf("{\"event\":\"native_thread_join\",\"token\":%llu}\n", token); return 0;
    });
}
int KT_SYSV AttributeInit(Token* output) { return Init(output); }
int KT_SYSV AttributeDestroy(Token* slot) { return DestroyAttribute(slot); }
int KT_SYSV AttributeStack(Token* slot, std::size_t bytes) { return Stack(slot, bytes); }
int KT_SYSV AttributeInherit(Token* slot, int mode) { return Inherit(slot, mode); }
int KT_SYSV AttributePriority(Token* slot, const Scheduling* scheduling) { return Priority(slot, scheduling, __builtin_return_address(0)); }
int KT_SYSV AttributeAffinity(Token* slot, Token mask) { return Affinity(slot, mask, __builtin_return_address(0)); }
int KT_SYSV ThreadCreate(Token* output, const Token* attr, Entry entry, void* arg, const char* name) {
    return Create(output, attr, entry, arg, name, __builtin_return_address(0));
}
int KT_SYSV ThreadJoin(Token token, void** result) { return Join(token, result, __builtin_return_address(0)); }

struct Fixture {
    HANDLE entered{}, release{};
    std::atomic<unsigned> calls{}, native_id{};
    std::atomic<int> self_join{};
    std::atomic<int> priority{THREAD_PRIORITY_ERROR_RETURN}, inherited_priority{THREAD_PRIORITY_ERROR_RETURN};
    std::atomic<int> guest_priority{}, inherited_guest_priority{};
};
void* KT_SYSV ReturnPriority(void* argument) {
    if (argument) { *static_cast<std::atomic<int>*>(argument) = NativeGuestThreadPriority(); }
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(GetThreadPriority(GetCurrentThread()) + 16));
}
void* KT_SYSV CountEntry(void* argument) { ++*static_cast<std::atomic<unsigned>*>(argument); return argument; }
void* KT_SYSV FixtureEntry(void* argument) {
    auto& state = *static_cast<Fixture*>(argument);
    ++state.calls; state.native_id.store(GetCurrentThreadId());
    state.self_join.store(Join(current_thread, nullptr, nullptr));
    state.priority = GetThreadPriority(GetCurrentThread());
    state.guest_priority = NativeGuestThreadPriority();
    Token child{}; void* priority_result{};
    if (Create(&child, nullptr, ReturnPriority, &state.inherited_guest_priority, nullptr, nullptr, true) == 0 && Join(child, &priority_result, nullptr) == 0) {
        state.inherited_priority = static_cast<int>(reinterpret_cast<std::uintptr_t>(priority_result)) - 16;
    }
    SetEvent(state.entered);
    if (WaitForSingleObject(state.release, 5000) != WAIT_OBJECT_0) { UnsupportedApi("thread_fixture_release_wait", 0, nullptr); }
    return argument;
}
void* KT_SYSV ReturnArgument(void* argument) { return argument; }
void* KT_SYSV ReturnAffinity(void*) {
    GROUP_AFFINITY actual{};
    if (!GetThreadGroupAffinity(GetCurrentThread(), &actual) || actual.Group != 0) { return nullptr; }
    return reinterpret_cast<void*>(actual.Mask);
}
}

std::uint64_t ResolveThreadImport(std::string_view identifier) {
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "nsYoNRywwNg") { return reinterpret_cast<std::uint64_t>(&AttributeInit); }
    if (id == "62KCwEMmzcM") { return reinterpret_cast<std::uint64_t>(&AttributeDestroy); }
    if (id == "UTXzJbWhhTE") { return reinterpret_cast<std::uint64_t>(&AttributeStack); }
    if (id == "eXbUSpEaTsA") { return reinterpret_cast<std::uint64_t>(&AttributeInherit); }
    if (id == "DzES9hQF4f4") { return reinterpret_cast<std::uint64_t>(&AttributePriority); }
    if (id == "3qxgM4ezETA") { return reinterpret_cast<std::uint64_t>(&AttributeAffinity); }
    if (id == "6UgtwV+0zb4") { return reinterpret_cast<std::uint64_t>(&ThreadCreate); }
    if (id == "onNY9Byn-W8") { return reinterpret_cast<std::uint64_t>(&ThreadJoin); }
    return 0;
}
int NativeGuestThreadPriority() {
    return current_guest_priority;
}
std::uint64_t NativeHostAffinityForDiagnostics(std::uint64_t guest) {
    DWORD_PTR available{}, system{};
    if (GetActiveProcessorGroupCount() != 1 ||
        !GetProcessAffinityMask(GetCurrentProcess(), &available, &system)) { return 0; }
    return MapAffinity(guest, available);
}
bool ThreadAffinitySelfTest() {
    bool passed = true;
    const auto check = [&](bool value, const char* name) {
        std::printf("{\"event\":\"thread_affinity_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false"); passed &= value;
    };
    Token attribute{}, unknown = 73;
    check(Affinity(nullptr, 1, nullptr) == kInvalid && Affinity(&unknown, 1, nullptr) == kInvalid && Init(&attribute) == 0,
          "invalid_attribute_rejected_before_mutation");
    check(Affinity(&attribute, 0, nullptr) == kInvalid && Affinity(&attribute, 0x40, nullptr) == kUnsupported &&
          Affinity(&attribute, Token{1} << 63, nullptr) == kUnsupported && registry.attributes.at(attribute).affinity == 0,
          "zero_and_outside_profile_masks_do_not_change_default");
    check(MapAffinity(1, 0x900) == 0x100 && MapAffinity(2, 0x900) == 0x800 && MapAffinity(4, 0x900) == 0x100 &&
          MapAffinity(0x3f, 0x900) == 0x900 && MapAffinity(0x20, Token{1} << 63) == (Token{1} << 63) &&
          !MapAffinity(1, 0) && !MapAffinity(0, 1) && !MapAffinity(0x40, 1), "sparse_and_small_host_sets_map_virtual_cpus_without_invalid_host_bits");
    DWORD_PTR process_before{}, system_before{}; GROUP_AFFINITY caller_before{};
    const bool host = GetActiveProcessorGroupCount() == 1 && GetProcessAffinityMask(GetCurrentProcess(), &process_before, &system_before) &&
                      GetThreadGroupAffinity(GetCurrentThread(), &caller_before);
    check(host, "single_processor_group_backend_available");
    Token token = 73; std::atomic<unsigned> calls{};
    check(Affinity(&attribute, 1, nullptr) == 0 && Create(&token, &attribute, CountEntry, &calls, nullptr, nullptr, true, false, false, true) == kAgain &&
          token == 73 && calls == 0 && registry.pending == 0 && registry.threads.empty(), "failed_affinity_setup_cancels_before_guest_entry_or_publication");
    bool levels = host;
    for (const Token mask : {Token{1}, Token{2}, Token{0x20}, Token{0x3f}}) {
        void* result{}; levels &= Affinity(&attribute, mask, nullptr) == 0;
        const auto created = Create(&token, &attribute, ReturnAffinity, nullptr, nullptr, nullptr, true);
        levels &= created == 0;
        if (!created) { levels &= Join(token, &result, nullptr) == 0 && reinterpret_cast<Token>(result) == process_before; }
    }
    check(levels, "virtual_masks_preserve_the_observed_host_eligible_set_without_pinning");
    Fixture fixture{CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool snapshot = fixture.entered && fixture.release && Affinity(&attribute, 1, nullptr) == 0;
    const auto created = snapshot ? Create(&token, &attribute, FixtureEntry, &fixture, nullptr, nullptr, true) : kAgain;
    snapshot &= created == 0;
    if (!created) {
        snapshot &= WaitForSingleObject(fixture.entered, 3000) == WAIT_OBJECT_0 && Affinity(&attribute, 2, nullptr) == 0 && DestroyAttribute(&attribute) == 0;
        GROUP_AFFINITY actual{};
        { std::lock_guard lock(registry.gate); const auto& live = registry.threads.at(token);
          snapshot &= live->guest_affinity == 1 && GetThreadGroupAffinity(live->native, &actual) &&
                      actual.Mask == process_before && live->native_affinity == actual.Mask; }
        SetEvent(fixture.release); snapshot &= Join(token, nullptr, nullptr) == 0;
    }
    check(snapshot && attribute == 0, "attribute_changes_and_destruction_do_not_rebind_live_threads");
    if (fixture.entered) { CloseHandle(fixture.entered); } if (fixture.release) { CloseHandle(fixture.release); }
    DWORD_PTR process_after{}, system_after{}; GROUP_AFFINITY caller_after{};
    check(GetProcessAffinityMask(GetCurrentProcess(), &process_after, &system_after) && GetThreadGroupAffinity(GetCurrentThread(), &caller_after) &&
          process_before == process_after && system_before == system_after && caller_before.Group == caller_after.Group && caller_before.Mask == caller_after.Mask &&
          registry.threads.empty() && registry.pending == 0, "process_and_creator_affinity_unchanged_and_threads_retired");
    std::printf("{\"event\":\"thread_affinity_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
bool ThreadSelfTest() {
    bool passed = true;
    auto check = [&](bool condition, const char* name) {
        std::printf("{\"event\":\"thread_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, condition ? "true" : "false"); passed &= condition;
    };
    Token attribute = 0, token = 73;
    const auto process_class = GetPriorityClass(GetCurrentProcess());
    const auto creator_priority = GetThreadPriority(GetCurrentThread());
    std::atomic<int> unmanaged_priority{};
    std::thread unmanaged([&] { unmanaged_priority = NativeGuestThreadPriority(); }); unmanaged.join();
    check(NativeGuestThreadPriority() == 700 && unmanaged_priority == 700, "unmanaged_threads_keep_default_guest_priority");
    check(Init(nullptr) == kInvalid && Stack(nullptr, kDefaultStack) == kInvalid && DestroyAttribute(nullptr) == kInvalid, "null_attribute_validation");
    check(Init(&attribute) == 0 && registry.attributes.at(attribute).stack == kDefaultStack, "default_one_mib_stack_attribute");
    check(Stack(&attribute, kMinimumStack - 1) == kInvalid && registry.attributes.at(attribute).stack == kDefaultStack &&
          Stack(&attribute, 128 * 1024) == 0, "stack_minimum_and_attribute_update");
    Scheduling invalid_priority{255}, selected_priority{699};
    check(Inherit(nullptr, 0) == kInvalid && Inherit(&attribute, 7) == kUnsupported && registry.attributes.at(attribute).inherit == 4 &&
          Priority(&attribute, nullptr, nullptr) == kUnsupported && Priority(&attribute, &invalid_priority, nullptr) == kUnsupported &&
          registry.attributes.at(attribute).priority == 700 && Inherit(&attribute, 0) == 0 && Priority(&attribute, &selected_priority, nullptr) == 0,
          "scheduling_attribute_validation_and_explicit_snapshot");
    check(Create(&token, &attribute, nullptr, nullptr, nullptr, nullptr) == kInvalid && token == 73 &&
          Create(&token, &attribute, ReturnArgument, nullptr, nullptr, nullptr) == kInvalid, "callback_must_be_executable_guest_code");
    check(Create(&token, &attribute, ReturnArgument, nullptr, nullptr, nullptr, true, true) == kMemory && token == 73 &&
          registry.pending == 0 && registry.threads.empty(), "failed_publication_cancels_and_joins_host_trampoline");
    std::atomic<unsigned> callbacks{};
    check(Create(&token, &attribute, CountEntry, &callbacks, nullptr, nullptr, true, false, true) == kAgain && token == 73 && callbacks == 0 &&
          registry.pending == 0 && registry.threads.empty(), "failed_priority_setup_never_publishes_or_runs_guest_entry");
    Fixture fixture{CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!fixture.entered || !fixture.release) { return false; }
    check(Create(&token, &attribute, FixtureEntry, &fixture, "KetsuiThreadTest", nullptr, true) == 0 && token != 73,
          "real_thread_created_with_explicit_stack");
    check(WaitForSingleObject(fixture.entered, 3000) == WAIT_OBJECT_0 && fixture.calls == 1 &&
          fixture.native_id != GetCurrentThreadId(), "sysv_callback_runs_on_distinct_windows_thread");
    check(fixture.self_join == kDeadlock, "self_join_rejected_without_blocking");
    check(fixture.priority == THREAD_PRIORITY_ABOVE_NORMAL && fixture.inherited_priority == THREAD_PRIORITY_ABOVE_NORMAL,
          "explicit_699_and_nested_inheritance_apply_real_windows_thread_priority");
    check(fixture.guest_priority == 699 && fixture.inherited_guest_priority == 699,
          "thread_local_guest_priority_matches_explicit_and_inherited_snapshots");
    check(DestroyAttribute(&attribute) == 0 && attribute == 0 && DestroyAttribute(&attribute) == kInvalid,
          "attribute_destruction_does_not_change_live_thread");
    void* result = nullptr;
    std::atomic<int> joined{kInvalid};
    std::thread joiner([&] { joined.store(Join(token, &result, nullptr)); });
    bool waiting = false;
    for (unsigned i = 0; i < 3000 && !waiting; ++i) {
        { std::lock_guard lock(registry.gate); waiting = registry.threads.at(token)->joining; }
        if (!waiting) { Sleep(1); }
    }
    check(waiting && Join(token, nullptr, nullptr) == kInvalid && joined == kInvalid, "join_waits_for_real_completion_and_has_one_owner");
    SetEvent(fixture.release); joiner.join();
    check(joined == 0 && result == &fixture && registry.threads.empty() && Join(token, nullptr, nullptr) == kSearch,
          "join_returns_full_pointer_and_retires_handle");
    bool cycles = true;
    const auto old = token;
    for (unsigned i = 0; i < 16; ++i) {
        const auto created = Create(&token, nullptr, ReturnArgument, &fixture, nullptr, nullptr, true);
        if (created || Join(token, &result, nullptr) != 0 || result != &fixture || token <= old) { cycles = false; break; }
    }
    check(cycles && registry.threads.empty() && registry.pending == 0, "fast_return_default_stack_and_repeated_join_lifetimes");
    bool levels = Init(&attribute) == 0 && Inherit(&attribute, 0) == 0;
    for (const auto priority : {256, 699, 700, 767}) {
        Scheduling selected{priority}; void* actual{};
        std::atomic<int> guest_priority{};
        levels &= Priority(&attribute, &selected, nullptr) == 0;
        const auto created = Create(&token, &attribute, ReturnPriority, &guest_priority, nullptr, nullptr, true);
        levels &= created == 0;
        if (!created) { levels &= Join(token, &actual, nullptr) == 0 && static_cast<int>(reinterpret_cast<std::uintptr_t>(actual)) - 16 == WindowsPriority(priority) && guest_priority == priority; }
    }
    check(levels && DestroyAttribute(&attribute) == 0 && GetPriorityClass(GetCurrentProcess()) == process_class &&
          GetThreadPriority(GetCurrentThread()) == creator_priority, "all_four_native_priority_tiers_without_process_or_creator_priority_change");
    CloseHandle(fixture.entered); CloseHandle(fixture.release);
    std::printf("{\"event\":\"thread_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
