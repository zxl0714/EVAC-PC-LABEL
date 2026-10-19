#include "native_np.h"
#include "native_np_score.h"
#include "native_memory.h"
#include "native_diagnostics.h"
#include "native_user.h"
#include "native_trophy.h"
#include <windows.h>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <new>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <stdexcept>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
constexpr int kArgument = static_cast<int>(0x80550003u);
constexpr int kNoUser = static_cast<int>(0x80550007u);
constexpr int kNoCallback = static_cast<int>(0x80550009u);
constexpr int kLimit = static_cast<int>(0x8055001du);
constexpr int kSignedOut = 1;
constexpr int kCommunityReady = 2;
constexpr int kOffline = static_cast<int>(0x80550006u);
std::atomic<std::shared_ptr<kt::community::Client>> community;
std::mutex community_gate;
unsigned community_score_references{};
kt::community::Identity CommunitySnapshot() noexcept {
    try {
        const auto client = community.load();
        return client ? client->State() : kt::community::Identity{};
    } catch (...) { return {}; }
}
int KT_SYSV NetInitOffline() {
    // No socket service, network pool, SSL or HTTP context is created. The
    // game's online initializer checks this encoded failure before proceeding.
    TraceNativeApi("sceNetInit_not_supported", 0, __builtin_return_address(0));
    return static_cast<int>(0x8041014eu); // SCE_NET_ERROR_ENOSYS
}
int KT_SYSV HttpTermOffline(int id) {
    // NetInitOffline never creates a pool or an HTTP context. The game's
    // +0x13cd50 destructor still calls all cleanup APIs after init fails.
    TraceNativeApi("sceHttpTerm_before_init", static_cast<unsigned>(id), __builtin_return_address(0));
    return static_cast<int>(0x80431001u); // HTTP_ERROR_BEFORE_INIT
}
int KT_SYSV SslTermOffline(int id) {
    TraceNativeApi("sceSslTerm_not_supported", static_cast<unsigned>(id), __builtin_return_address(0));
    // No SSL provider is installed. This is the runtime's encoded ENOSYS,
    // not a claim about the console's undocumented SSL invalid-ID value.
    return static_cast<int>(0x8002004eu);
}
int KT_SYSV NetPoolDestroyOffline(int id) {
    TraceNativeApi("sceNetPoolDestroy_bad_id", static_cast<unsigned>(id), __builtin_return_address(0));
    return static_cast<int>(0x80410109u); // NET_ERROR_EBADF: no pool was issued
}
int KT_SYSV NetTermOffline() {
    // Idempotent teardown of the empty service; no Winsock/threads/handles
    // exist to release. NetInit remains unsupported on every subsequent call.
    TraceNativeApi("sceNetTerm_empty", 0, __builtin_return_address(0));
    return 0;
}
constexpr int kMemory = static_cast<int>(0x80550005u);
constexpr int kRequestLimit = static_cast<int>(0x80550013u), kNoRequest = static_cast<int>(0x80550014u);
constexpr int kSize = static_cast<int>(0x80550011u), kAborted = static_cast<int>(0x80550012u);
constexpr int kInvalidId = static_cast<int>(0x80550015u), kTitleId = static_cast<int>(0x8055001eu);
struct AsyncParameters { std::uint64_t size, affinity; std::int32_t priority; unsigned char padding[4]; };
struct AgeRestriction { char country[4]; signed char age; unsigned char padding[3]; };
struct ContentRestriction { std::uint64_t size; signed char age; unsigned char padding[3]; std::int32_t count; const AgeRestriction* countries; };
struct TitleId { char text[13]; unsigned char padding[3]; };
struct TitleSecret { unsigned char bytes[128]{}; ~TitleSecret() { SecureZeroMemory(bytes, sizeof(bytes)); } };
static_assert(sizeof(AsyncParameters) == 24 && offsetof(AsyncParameters, priority) == 16);
static_assert(sizeof(AgeRestriction) == 8 && sizeof(ContentRestriction) == 24 && offsetof(ContentRestriction, countries) == 16);
static_assert(sizeof(TitleId) == 16 && sizeof(TitleSecret) == 128);
bool ReadGuest(const void* source, void* destination, std::size_t bytes) {
    SIZE_T read{};
    return source && ReadProcessMemory(GetCurrentProcess(), source, destination, bytes, &read) && read == bytes;
}
bool WritableGuest(const void* destination, std::size_t bytes) {
    return destination && NativeMemoryWritable(destination, bytes);
}
constexpr int kNetAddress = static_cast<int>(0x80412107u);
int KT_SYSV NetworkState(int* output) {
    if (!WritableGuest(output, sizeof(*output))) { return kNetAddress; }
    // A configured provider reports its own verified transport, not a Windows
    // adapter snapshot or Sony authentication. Default startup stays offline.
    const int current = CommunitySnapshot().ready ? 3 : 0;
    if (!CopyToNativeMemory(output, &current, sizeof(current))) { return kNetAddress; }
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) { TraceNativeApi(current ? "sceNetCtlGetState_community" : "sceNetCtlGetState_disconnected", static_cast<unsigned>(current), __builtin_return_address(0)); }
    return 0;
}
struct Registration {
    std::mutex mutex;
    bool content_set{}, title_set{};
    signed char age{};
    std::int32_t count{};
    std::array<AgeRestriction, 256> countries{};
    TitleId title{};
    TitleSecret secret;
    std::uint64_t generation{};
} registration;
std::atomic<unsigned> registration_traces{};
int KT_SYSV SetContentRestriction(const ContentRestriction* input) {
    ContentRestriction snapshot{};
    if (!ReadGuest(input, &snapshot, sizeof(snapshot))) { return kArgument; }
    if (snapshot.size != sizeof(snapshot)) { return kSize; }
    if (snapshot.age < 0 || snapshot.count < 0 || snapshot.count > 256) { return kArgument; }
    std::array<AgeRestriction, 256> countries{};
    if (snapshot.count && !ReadGuest(snapshot.countries, countries.data(), snapshot.count * sizeof(AgeRestriction))) { return kArgument; }
    for (int i = 0; i < snapshot.count; ++i) { if (countries[i].age < 0) { return kArgument; } }
    std::lock_guard lock(registration.mutex);
    registration.age = snapshot.age; registration.count = snapshot.count; registration.countries = countries;
    registration.content_set = true; ++registration.generation;
    if (registration_traces.fetch_add(1) < 16) {
        TraceNativeApi("sceNpSetContentRestriction", static_cast<unsigned>(snapshot.count), __builtin_return_address(0));
    }
    // Application rating metadata, not a change to any user's parental controls.
    return 0;
}
int KT_SYSV SetTitleId(const TitleId* input, const TitleSecret* secret) {
    TitleId title{}; TitleSecret copied;
    if (!ReadGuest(input, &title, sizeof(title)) || !ReadGuest(secret, copied.bytes, sizeof(copied.bytes))) { return kArgument; }
    const auto length = strnlen_s(title.text, sizeof(title.text));
    if (!length || length == sizeof(title.text)) { return kTitleId; }
    for (std::size_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(title.text[i]) < 32 || static_cast<unsigned char>(title.text[i]) >= 127) { return kTitleId; }
    }
    std::lock_guard lock(registration.mutex);
    registration.title = title;
    SecureZeroMemory(registration.secret.bytes, sizeof(registration.secret.bytes));
    std::memcpy(registration.secret.bytes, copied.bytes, sizeof(copied.bytes));
    registration.title_set = true; ++registration.generation;
    if (registration_traces.fetch_add(1) < 16) {
        TraceNativeApi("sceNpSetNpTitleId_local_metadata", 0, __builtin_return_address(0));
    }
    // No sign-in or transport is created. Neither ID nor secret is logged.
    return 0;
}
struct Request { int id{}; bool complete{}, async{}, aborted{}; int result{}; std::uint64_t affinity{}; std::int32_t priority{}; };
struct Requests {
    std::mutex mutex;
    std::array<Request, 32> entries{};
    int next = 0x20000001;
} requests;
struct Language { char code[6], padding[10]; };
struct ParentalInfo { unsigned char content, chat, user_content; };
static_assert(sizeof(Language) == 16 && sizeof(ParentalInfo) == 3);
std::atomic<unsigned> request_traces{}, completion_traces{};
template<typename Function> int RequestApi(const void* caller, Function&& function) {
    try { return function(); }
    catch (const std::bad_alloc&) { return kMemory; }
    catch (...) { UnsupportedApi("native_np_request_host_failure", 0, caller); }
}
int AllocateRequest(bool async, std::uint64_t affinity, std::int32_t priority, const void* caller) {
    return RequestApi(caller, [&] {
        std::lock_guard lock(requests.mutex);
        if (requests.next == (std::numeric_limits<int>::max)()) { return kRequestLimit; }
        for (auto& request : requests.entries) {
            if (request.id) { continue; }
            const auto id = requests.next;
            if (request_traces.fetch_add(1) < 16) { TraceNativeApi(async ? "sceNpCreateAsyncRequest" : "sceNpCreateRequest", static_cast<unsigned>(id), caller); }
            request = {id, false, async, false, 0, affinity, priority}; ++requests.next; return id;
        }
        return kRequestLimit;
    });
}
int KT_SYSV CreateRequest() { return AllocateRequest(false, 0, 0, __builtin_return_address(0)); }
int KT_SYSV CreateAsyncRequest(const AsyncParameters* input) {
    AsyncParameters parameters{};
    if (!ReadGuest(input, &parameters, sizeof(parameters))) { return kArgument; }
    if (parameters.size != sizeof(parameters)) { return kSize; }
    // Offline queries complete immediately; no worker thread needs scheduling.
    return AllocateRequest(true, parameters.affinity, parameters.priority, __builtin_return_address(0));
}
int KT_SYSV AbortRequest(int id) {
    return RequestApi(__builtin_return_address(0), [&] {
        std::lock_guard lock(requests.mutex);
        for (auto& request : requests.entries) {
            if (!request.id || request.id != id) { continue; }
            if (!request.complete) { request.aborted = true; request.complete = true; request.result = kAborted; }
            return 0;
        }
        return kNoRequest;
    });
}
int KT_SYSV PollAsync(int id, int* output) {
    if (!WritableGuest(output, sizeof(*output))) { return kArgument; }
    return RequestApi(__builtin_return_address(0), [&] {
        std::lock_guard lock(requests.mutex);
        for (const auto& request : requests.entries) {
            if (!request.id || request.id != id) { continue; }
            if (!request.async || !request.complete) { return kInvalidId; }
            std::memcpy(output, &request.result, sizeof(request.result)); return 0;
        }
        return kNoRequest;
    });
}
int KT_SYSV DeleteRequest(int id) {
    return RequestApi(__builtin_return_address(0), [&] {
        std::lock_guard lock(requests.mutex);
        for (auto& request : requests.entries) { if (request.id && request.id == id) { request = {}; return 0; } }
        return kNoRequest;
    });
}
int CompleteOffline(int id, int user, const char* operation, const void* caller, int result = kOffline) {
    if (!id || user == -1) { return kArgument; }
    if (user != 0x1000 || !NativeUserInitialized()) { return kNoUser; }
    std::lock_guard lock(requests.mutex);
    for (auto& request : requests.entries) {
        if (!request.id || request.id != id) { continue; }
        if (request.aborted) { return kAborted; }
        if (request.complete) { return kArgument; }
        // Only service availability may reflect the explicit community
        // provider. Sony account language, age and restrictions stay unknown.
        if (completion_traces.fetch_add(1) < 16) {
            TraceNativeApi(operation, static_cast<unsigned>(id), caller);
            std::printf("{\"event\":\"native_np_request_complete\",\"operation\":\"%s\",\"id\":%d,\"user\":%d,\"result\":%d,\"online\":%s,\"psn_authenticated\":false}\n", operation, id, user, result, result == 0 ? "true" : "false");
        }
        request.complete = true; request.result = result; return request.async ? 0 : result;
    }
    return kNoRequest;
}
int KT_SYSV AccountLanguage(int id, int user, Language* output) {
    if (!output) { return kArgument; }
    const auto caller = __builtin_return_address(0);
    return RequestApi(caller, [&] { return CompleteOffline(id, user, "sceNpGetAccountLanguageA", caller); });
}
int KT_SYSV ParentalControl(int id, int user, signed char* age, ParentalInfo* output) {
    if (!age || !output) { return kArgument; }
    const auto caller = __builtin_return_address(0);
    return RequestApi(caller, [&] { return CompleteOffline(id, user, "sceNpGetParentalControlInfoA", caller); });
}
int KT_SYSV Availability(int id, int user) {
    const auto caller = __builtin_return_address(0);
    return RequestApi(caller, [&] { return CompleteOffline(id, user, "sceNpCheckNpAvailabilityA", caller, CommunitySnapshot().ready ? 0 : kOffline); });
}
int KT_SYSV Reachability(int user, int* output) {
    if (!output || user == -1) { return kArgument; }
    if (user != 0x1000 || !NativeUserInitialized()) { return kNoUser; }
    // Native reachability enum: Unavailable=0, Available=1, Reachable=2.
    const int current = CommunitySnapshot().ready ? 2 : 0;
    return CopyToNativeMemory(output, &current, sizeof(current)) ? 0 : kArgument;
}
int KT_SYSV AccountCountry(int user, void* output) {
    if (!output || user == -1) { return kArgument; }
    if (user != 0x1000 || !NativeUserInitialized()) { return kNoUser; }
    return kOffline;
}
int KT_SYSV AccountId(int user, std::uint64_t* output) {
    if (!output || user == -1) { return kArgument; }
    if (user != 0x1000 || !NativeUserInitialized()) { return kNoUser; }
    const auto current = CommunitySnapshot();
    if (!current.ready) { return kOffline; }
    return CopyToNativeMemory(output, &current.account, sizeof(current.account)) ? 0 : kArgument;
}
using Callback = void (KT_SYSV *)(int, int, void*);
struct Subscription { int id{}; Callback callback{}; void* argument{}; };
struct StateEvent { int user{}, value{}, last_registration{}; };
struct Notifications {
    std::recursive_mutex dispatch;
    std::mutex mutex;
    std::array<Subscription, 8> entries{};
    std::array<StateEvent, 64> events{};
    unsigned event_count{};
    int next = 1;
    bool community_ready{};
};
Notifications& State() { static Notifications notifications; return notifications; }
thread_local bool dispatching{};
int Register(Callback callback, void* argument, const void* caller) {
    if (!callback) { return kArgument; }
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (state.next == (std::numeric_limits<int>::max)()) { return kLimit; }
    for (auto& slot : state.entries) {
        if (slot.id) { continue; }
        // Registration is not a sign-out transition. GetState supplies the
        // current offline snapshot; no invented event is sent to the game.
        slot = {state.next++, callback, argument};
        TraceNativeApi("sceNpRegisterStateCallbackA", static_cast<unsigned>(slot.id), caller);
        return slot.id;
    }
    return kLimit;
}
int Unregister(int id) {
    auto& state = State();
    // Serializes cancellation with callbacks on other threads; recursive for
    // a callback that cancels itself or another pending registration.
    std::lock_guard dispatcher(state.dispatch);
    std::lock_guard lock(state.mutex);
    for (auto& slot : state.entries) {
        if (id > 0 && slot.id == id) { slot = {}; return 0; }
    }
    return kNoCallback;
}
int QueueStateEvent(int user, int value) {
    // The offline provider never produces a transition. Authored self-tests
    // exercise dispatch here; a future transport must enqueue real changes,
    // not manufacture SignedIn just to bypass the game's online checks.
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (state.event_count == state.events.size()) { return kLimit; }
    state.events[state.event_count++] = {user, value, state.next - 1};
    return 0;
}
int Pump() {
    if (dispatching) { return 0; }
    auto& state = State(); std::lock_guard dispatcher(state.dispatch);
    struct DispatchScope { DispatchScope() { dispatching = true; } ~DispatchScope() { dispatching = false; } } scope;
    if (NativeUserInitialized()) {
        const auto current = CommunitySnapshot().ready;
        bool changed{};
        {
            std::lock_guard lock(state.mutex);
            changed = current != state.community_ready;
        }
        if (changed) {
            const auto queued = QueueStateEvent(0x1000, current ? kCommunityReady : kSignedOut);
            if (queued != 0) { return queued; }
            std::lock_guard lock(state.mutex); state.community_ready = current;
            std::printf("{\"event\":\"native_community_state\",\"ready\":%s,\"psn_authenticated\":false}\n", current ? "true" : "false");
        }
    }
    std::array<StateEvent, 64> events{}; unsigned count{};
    {
        std::lock_guard lock(state.mutex);
        if (!NativeUserInitialized()) { return 0; }
        events = state.events; count = state.event_count; state.event_count = 0;
    }
    for (unsigned index = 0; index < count; ++index) {
        const auto event = events[index];
        std::array<int, 8> ids{};
        {
            std::lock_guard lock(state.mutex);
            for (std::size_t i = 0; i < ids.size(); ++i) {
                const auto id = state.entries[i].id;
                if (id <= event.last_registration) { ids[i] = id; }
            }
        }
        for (const auto id : ids) {
            Subscription current{};
            {
                std::lock_guard lock(state.mutex);
                for (const auto& slot : state.entries) {
                    if (id && slot.id == id) { current = slot; break; }
                }
            }
            // No registry lock spans original machine code. Registrations and
            // events created by callbacks cannot re-enter this captured batch.
            if (current.callback) { current.callback(event.user, event.value, current.argument); }
        }
    }
    return 0;
}
int KT_SYSV GetState(int user, int* output) {
    if (!output || user == -1) { return kArgument; }
    if (user != 0x1000 || !NativeUserInitialized()) { return kNoUser; }
    const auto current = CommunitySnapshot().ready ? kCommunityReady : kSignedOut;
    return CopyToNativeMemory(output, &current, sizeof(current)) ? 0 : kArgument;
}
int KT_SYSV RegisterCallback(Callback callback, void* argument) { return Register(callback, argument, __builtin_return_address(0)); }
int KT_SYSV UnregisterCallback(int id) { return Unregister(id); }
int KT_SYSV CheckCallback() { return Pump(); }

struct TestCall {
    int count{}, user{}, state{}, id{}, second{}, registered{};
    bool recursive{}, cancel_next{};
    std::array<int, 64> states{};
};
void KT_SYSV TestCallback(int user, int state, void* argument) {
    auto& record = *static_cast<TestCall*>(argument);
    ++record.count; record.user = user; record.state = state;
    if (record.count <= static_cast<int>(record.states.size())) { record.states[record.count - 1] = state; }
    if (record.recursive) {
        (void)Unregister(record.id);
        record.registered = Register(TestCallback, argument, nullptr);
        (void)QueueStateEvent(0x1000, kSignedOut); // Authored event, not registration side effect.
        record.recursive = false;
        (void)Pump();
    }
    if (record.cancel_next) { (void)Unregister(record.second); }
}
}

bool ConfigureNativeCommunity(std::shared_ptr<kt::community::Client> client) {
    std::lock_guard lock(community_gate);
    if (!client || community.load() || !ConfigureNativeScore(client)) { return false; }
    community.store(std::move(client));
    std::puts("{\"event\":\"native_community_provider\",\"scope\":\"development_loopback\",\"psn_authenticated\":false}");
    return true;
}
int CommunityScoreModule(CommunityScoreModuleAction action) {
    constexpr int missing = static_cast<int>(0x805a1001u), busy = static_cast<int>(0x80020010u);
    std::lock_guard lock(community_gate);
    if (!community.load()) {
        if (action == CommunityScoreModuleAction::Load) { TraceNativeApi("sceSysmoduleLoadModule_unavailable", 0x1e, nullptr); }
        return missing;
    }
    if (action == CommunityScoreModuleAction::IsLoaded) { return community_score_references ? 0 : missing; }
    if (action == CommunityScoreModuleAction::Load) {
        if (community_score_references == 0xffffffffu) { return kMemory; }
        ++community_score_references;
    } else {
        if (!community_score_references) { return missing; }
        if (community_score_references == 1 && NativeScoreContextsLive()) { return busy; }
        --community_score_references;
    }
    std::printf("{\"event\":\"native_community_score_module\",\"loaded\":%s,\"references\":%u,\"psn_authenticated\":false}\n",
                community_score_references ? "true" : "false", community_score_references);
    return 0;
}

std::uint64_t ResolveNpImport(std::string_view identifier, std::string_view library, std::string_view module) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (library == "libSceHttp" && module == "libSceHttp" && hash == "Ik-KpLTlf7Q") {
        return reinterpret_cast<std::uint64_t>(&HttpTermOffline);
    }
    if (library == "libSceSsl" && module == "libSceSsl" && hash == "0K1yQ6Lv-Yc") {
        return reinterpret_cast<std::uint64_t>(&SslTermOffline);
    }
    if (library == "libSceNet" && module == "libSceNet") {
        if (hash == "K7RlrTkI-mw") { return reinterpret_cast<std::uint64_t>(&NetPoolDestroyOffline); }
        if (hash == "cTGkc6-TBlI") { return reinterpret_cast<std::uint64_t>(&NetTermOffline); }
    }
    if (library == "libSceNetCtl" && module == "libSceNetCtl" && identifier.substr(0, identifier.find('#')) == "uBPlr0lbuiI") {
        return reinterpret_cast<std::uint64_t>(&NetworkState);
    }
    if (library == "libSceNet" && module == "libSceNet" && identifier.substr(0, identifier.find('#')) == "Nlev7Lg8k3A") {
        return reinterpret_cast<std::uint64_t>(&NetInitOffline);
    }
    if (library == "libSceNpTrophy") { return ResolveTrophyImport(identifier,library,module); }
    if (library == "libSceNpScore") { return ResolveScoreImport(identifier,library,module); }
    if (library != "libSceNpManager" || module != "libSceNpManager") { return 0; }
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "qQJfO8HAiaY") { return reinterpret_cast<std::uint64_t>(&RegisterCallback); }
    if (id == "M3wFXbYQtAA") { return reinterpret_cast<std::uint64_t>(&UnregisterCallback); }
    if (id == "3Zl8BePTh9Y") { return reinterpret_cast<std::uint64_t>(&CheckCallback); }
    if (id == "eQH7nWPcAgc") { return reinterpret_cast<std::uint64_t>(&GetState); }
    if (id == "GpLQDNKICac") { return reinterpret_cast<std::uint64_t>(&CreateRequest); }
    if (id == "eiqMCt9UshI") { return reinterpret_cast<std::uint64_t>(&CreateAsyncRequest); }
    if (id == "OzKvTvg3ZYU") { return reinterpret_cast<std::uint64_t>(&AbortRequest); }
    if (id == "uqcPJLWL08M") { return reinterpret_cast<std::uint64_t>(&PollAsync); }
    if (id == "A2CQ3kgSopQ") { return reinterpret_cast<std::uint64_t>(&SetContentRestriction); }
    if (id == "Ec63y59l9tw") { return reinterpret_cast<std::uint64_t>(&SetTitleId); }
    if (id == "S7QTn72PrDw") { return reinterpret_cast<std::uint64_t>(&DeleteRequest); }
    if (id == "TPMbgIxvog0") { return reinterpret_cast<std::uint64_t>(&AccountLanguage); }
    if (id == "m9L3O6yst-U") { return reinterpret_cast<std::uint64_t>(&ParentalControl); }
    if (id == "8Z2Jc5GvGDI") { return reinterpret_cast<std::uint64_t>(&Availability); }
    if (id == "e-ZuhGEoeC4") { return reinterpret_cast<std::uint64_t>(&Reachability); }
    if (id == "JT+t00a3TxA") { return reinterpret_cast<std::uint64_t>(&AccountCountry); }
    if (id == "rbknaUjpqWo") { return reinterpret_cast<std::uint64_t>(&AccountId); }
    return 0;
}
bool NpSelfTest() {
    bool okay = true; int passed{};
    const auto check = [&](bool value, const char* name) {
        std::printf("{\"event\":\"np_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
        okay &= value; if (value) { ++passed; }
    };
    int output = 73;
    check(Register(nullptr, nullptr, nullptr) == kArgument && Unregister(0) == kNoCallback &&
          GetState(0x1000, &output) == kNoUser && output == 73, "validation_preserves_output");
    TestCall record;
    const auto id = Register(TestCallback, &record, nullptr);
    (void)Pump();
    check(record.count == 0, "no_callback_before_local_login");
    using Initialize = int (KT_SYSV *)(const void*);
    const auto initialize = reinterpret_cast<Initialize>(ResolveUserImport("j3YMu1MVNNo", "libSceUserService", "libSceUserService"));
    const auto ready = initialize(nullptr);
    (void)Pump(); (void)Pump();
    check(ready == 0 && record.count == 0 && GetState(0x1000, &output) == 0 && output == kSignedOut,
          "local_login_and_registration_do_not_invent_np_sign_out_events");
    (void)QueueStateEvent(0x1000, kSignedOut);
    (void)Pump(); (void)Pump();
    check(ready == 0 && record.count == 1 && record.user == 0x1000 && record.state == kSignedOut &&
          GetState(0x1000, &output) == 0 && output == kSignedOut, "explicit_event_delivered_once_with_user_data");
    check(Unregister(id) == 0 && Unregister(id) == kNoCallback, "stale_handle_rejected");
    record = {}; record.recursive = true; record.id = Register(TestCallback, &record, nullptr);
    (void)QueueStateEvent(0x1000, kSignedOut);
    (void)Pump();
    check(record.count == 1 && record.registered > record.id, "recursive_pump_and_registration_deferred");
    (void)Pump();
    check(record.count == 2 && Unregister(record.registered) == 0, "next_pump_delivers_new_registration");
    TestCall first, second; first.cancel_next = true;
    const auto first_id = Register(TestCallback, &first, nullptr);
    first.second = Register(TestCallback, &second, nullptr);
    (void)QueueStateEvent(0x1000, kSignedOut);
    (void)Pump();
    check(first.count == 1 && second.count == 0 && Unregister(first_id) == 0, "cancel_pending_during_callback");
    std::array<int, 8> ids{};
    for (auto& slot : ids) { slot = Register(TestCallback, &record, nullptr); }
    const bool full = Register(TestCallback, &record, nullptr) == kLimit;
    bool released = true;
    for (auto slot : ids) { released &= slot > 0 && Unregister(slot) == 0; }
    check(full && released, "bounded_registration_capacity");
    TestCall ordered;
    const auto ordered_id = Register(TestCallback, &ordered, nullptr);
    (void)QueueStateEvent(0x1000, 2); (void)QueueStateEvent(0x1000, kSignedOut); (void)Pump();
    check(ordered.count == 2 && ordered.states[0] == 2 && ordered.states[1] == kSignedOut &&
          Unregister(ordered_id) == 0 && GetState(0x1000, &output) == 0 && output == kSignedOut,
          "authored_event_order_does_not_change_real_offline_state");
    (void)QueueStateEvent(0x1000, kSignedOut);
    TestCall late; const auto late_id = Register(TestCallback, &late, nullptr); (void)Pump();
    check(late.count == 0 && Unregister(late_id) == 0, "new_subscriber_does_not_receive_past_events");
    TestCall bounded; const auto bounded_id = Register(TestCallback, &bounded, nullptr);
    bool queued = true;
    for (unsigned i = 0; i < 64; ++i) { queued &= QueueStateEvent(0x1000, kSignedOut) == 0; }
    queued &= QueueStateEvent(0x1000, kSignedOut) == kLimit;
    (void)Pump(); (void)Pump();
    check(queued && bounded.count == 64 && Unregister(bounded_id) == 0,
          "bounded_event_queue_reports_overflow_and_drains_once");
    AgeRestriction country{{'J', 'P', 0, 0}, 17, {}};
    ContentRestriction content{sizeof(ContentRestriction), 12, {}, 1, &country};
    const auto content_result = SetContentRestriction(&content);
    country.age = 3;
    check(content_result == 0 && registration.content_set && registration.age == 12 && registration.count == 1 &&
          registration.countries[0].age == 17, "application_ratings_are_copied_not_user_parental_settings");
    const auto generation = registration.generation;
    content.size = 23;
    bool rejected = SetContentRestriction(&content) == kSize;
    content.size = sizeof(content); content.count = -1;
    rejected &= SetContentRestriction(&content) == kArgument;
    content.count = 257; rejected &= SetContentRestriction(&content) == kArgument;
    content.count = 1; content.countries = nullptr; rejected &= SetContentRestriction(&content) == kArgument;
    content.count = 0; content.age = -1; rejected &= SetContentRestriction(&content) == kArgument;
    check(rejected && registration.generation == generation && registration.countries[0].age == 17,
          "invalid_rating_updates_preserve_complete_previous_registration");
    check(SetContentRestriction(nullptr) == kArgument && SetContentRestriction(reinterpret_cast<const ContentRestriction*>(1)) == kArgument,
          "rating_pointer_validation_does_not_fault");
    content.age = 0; content.countries = reinterpret_cast<const AgeRestriction*>(1);
    check(SetContentRestriction(&content) == 0 && registration.count == 0 && registration.age == 0,
          "empty_country_rules_do_not_dereference_unused_pointer");
    TitleId title{{'F', 'I', 'X', 'T', 'U', 'R', 'E', '_', '0', '0', '0', '1', 0}, {}};
    TitleSecret secret; std::memset(secret.bytes, 0x5a, sizeof(secret.bytes));
    const auto title_result = SetTitleId(&title, &secret);
    title.text[0] = 'X'; secret.bytes[0] = 0;
    check(title_result == 0 && registration.title_set && registration.title.text[0] == 'F' && registration.secret.bytes[0] == 0x5a,
          "title_registration_owns_metadata_and_secret_without_logging_them");
    const auto title_generation = registration.generation;
    std::memset(title.text, 'X', sizeof(title.text));
    check(SetTitleId(&title, &secret) == kTitleId && SetTitleId(nullptr, &secret) == kArgument &&
          SetTitleId(&registration.title, reinterpret_cast<const TitleSecret*>(1)) == kArgument &&
          registration.generation == title_generation && registration.secret.bytes[0] == 0x5a,
          "bad_title_registration_is_transactional");
    AsyncParameters parameters{sizeof(AsyncParameters), 0x3f, 700, {}};
    check(CreateAsyncRequest(nullptr) == kArgument && CreateAsyncRequest(reinterpret_cast<const AsyncParameters*>(1)) == kArgument &&
          ([&] { auto invalid = parameters; invalid.size = 23; return CreateAsyncRequest(&invalid) == kSize; })(),
          "async_creation_validates_full_24_byte_parameter_snapshot");
    const auto async = CreateAsyncRequest(&parameters);
    const auto sync = CreateRequest();
    struct { int value = 73, tail = 73; } result;
    bool attributes = false;
    { std::lock_guard lock(requests.mutex); for (const auto& request : requests.entries) {
        if (request.id == async) attributes = request.async && request.affinity == 0x3f && request.priority == 700;
    } }
    check(async > 0 && attributes && PollAsync(async, &result.value) == kInvalidId && PollAsync(sync, &result.value) == kInvalidId &&
          result.value == 73 && result.tail == 73, "ready_and_sync_requests_do_not_publish_async_result");
    check(Availability(async, 0x1000) == 0 && PollAsync(async, &result.value) == 0 && result.value == kOffline && result.tail == 73 &&
          GetState(0x1000, &output) == 0 && output == kSignedOut,
          "async_offline_query_is_accepted_then_poll_publishes_signed_out");
    check(AbortRequest(async) == 0 && PollAsync(async, &result.value) == 0 && result.value == kOffline &&
          DeleteRequest(async) == 0 && PollAsync(async, &result.value) == kNoRequest && AbortRequest(async) == kNoRequest && DeleteRequest(sync) == 0,
          "completed_abort_is_ignored_and_deleted_handles_are_stale");
    const auto aborted = CreateAsyncRequest(&parameters);
    check(AbortRequest(aborted) == 0 && AbortRequest(aborted) == 0 && PollAsync(aborted, &result.value) == 0 && result.value == kAborted &&
          Availability(aborted, 0x1000) == kAborted && DeleteRequest(aborted) == 0,
          "pending_abort_is_terminal_and_cannot_be_submitted_again");
    auto* readonly = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD protection{};
    const auto probe = CreateAsyncRequest(&parameters);
    bool output_guards = readonly && Availability(probe, 0x1000) == 0 && VirtualProtect(readonly, 4096, PAGE_READONLY, &protection) &&
        PollAsync(probe, static_cast<int*>(readonly)) == kArgument && PollAsync(probe, reinterpret_cast<int*>(1)) == kArgument &&
        PollAsync(probe, nullptr) == kArgument && PollAsync(probe, &result.value) == 0 && result.value == kOffline;
    if (readonly) { VirtualFree(readonly, 0, MEM_RELEASE); }
    check(output_guards && DeleteRequest(probe) == 0, "invalid_poll_outputs_do_not_consume_or_write_result");
    result = {73, 73};
    check(NetworkState(&result.value) == 0 && result.value == 0 && result.tail == 73 &&
          NetworkState(&result.value) == 0 && GetState(0x1000, &output) == 0 && output == kSignedOut,
          "network_status_is_disconnected_without_changing_np_sign_in");
    check(NetworkState(nullptr) == kNetAddress && NetworkState(reinterpret_cast<int*>(1)) == kNetAddress && result.tail == 73,
          "invalid_network_state_outputs_are_rejected_without_faulting");
    std::array<int, 32> async_ids{}; bool async_capacity = true;
    for (auto& request : async_ids) { request = CreateAsyncRequest(&parameters); async_capacity &= request > 0; }
    async_capacity &= CreateAsyncRequest(&parameters) == kRequestLimit;
    for (const auto request : async_ids) { async_capacity &= DeleteRequest(request) == 0; }
    check(async_capacity, "async_and_sync_share_bounded_request_registry");
    std::printf("{\"event\":\"np_self_test\",\"passed\":%d,\"success\":%s}\n", passed, okay ? "true" : "false");
    return okay;
}
bool NpRequestSelfTest() {
    bool passed = true;
    const auto check = [&](bool value, const char* name) {
        std::printf("{\"event\":\"np_request_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false"); passed &= value;
    };
    const auto first = CreateRequest();
    Language language{}; language.code[0] = 73;
    ParentalInfo info{73, 73, 73}; signed char age = 73;
    check(first > 0 && AccountLanguage(first, 0x1000, &language) == kNoUser && language.code[0] == 73,
          "creation_is_independent_of_local_login_but_query_is_not");
    using Initialize = int(KT_SYSV*)(const void*);
    const auto initialize = reinterpret_cast<Initialize>(ResolveUserImport("j3YMu1MVNNo", "libSceUserService", "libSceUserService"));
    check(initialize(nullptr) == 0 && AccountLanguage(first, -1, &language) == kArgument && AccountLanguage(first, 42, &language) == kNoUser &&
          AccountLanguage(first, 0x1000, nullptr) == kArgument && ParentalControl(first, 0x1000, nullptr, &info) == kArgument,
          "invalid_parameters_do_not_consume_ready_request");
    struct { int value = 73, tail = 73; } reachability;
    check(Reachability(-1, &reachability.value) == kArgument && Reachability(42, &reachability.value) == kNoUser && reachability.value == 73 &&
          Reachability(0x1000, &reachability.value) == 0 && reachability.value == 0 && reachability.tail == 73,
          "offline_reachability_writes_only_four_bytes_for_the_local_user");
    std::array<unsigned char, 4> country{73, 73, 73, 73};
    check(AccountCountry(-1, country.data()) == kArgument && AccountCountry(42, country.data()) == kNoUser && AccountCountry(0x1000, nullptr) == kArgument &&
          AccountCountry(0x1000, country.data()) == kOffline && std::all_of(country.begin(), country.end(), [](auto byte) { return byte == 73; }),
          "offline_country_query_preserves_entire_output");
    std::uint64_t account = 0x123456789abcdef0ull;
    check(AccountId(0x1000, &account) == kOffline && account == 0x123456789abcdef0ull, "no_online_account_id_is_fabricated");
    check(AccountLanguage(first, 0x1000, &language) == kOffline && language.code[0] == 73 &&
          AccountLanguage(first, 0x1000, &language) == kArgument && DeleteRequest(first) == 0 && DeleteRequest(first) == kNoRequest,
          "signed_out_completion_preserves_output_and_completed_request_cannot_be_reused");
    const auto second = CreateRequest();
    check(second > first && Availability(first, 0x1000) == kNoRequest && Availability(0, 0x1000) == kArgument &&
          ParentalControl(second, 0x1000, &age, &info) == kOffline && age == 73 && info.content == 73 && info.chat == 73 && info.user_content == 73 &&
          DeleteRequest(second) == 0, "stale_ids_are_not_reused_and_offline_parental_data_is_not_fabricated");
    std::array<int, 32> ids{}; bool filled = true;
    for (auto& id : ids) { id = CreateRequest(); filled &= id > 0; }
    check(filled && CreateRequest() == kRequestLimit, "exactly_thirty_two_live_requests_allowed");
    bool release = true; for (const auto id : ids) { release &= DeleteRequest(id) == 0; }
    const auto replacement = CreateRequest();
    check(release && replacement > ids.back() && Availability(replacement, 0x1000) == kOffline && DeleteRequest(replacement) == 0,
          "capacity_recovered_after_delete_and_availability_reports_signed_out");
    std::atomic<bool> concurrent_ok{true}; std::vector<std::thread> workers;
    for (unsigned i = 0; i < 8; ++i) {
        workers.emplace_back([&] {
            for (unsigned n = 0; n < 100; ++n) {
                const auto id = CreateRequest();
                if (id <= 0 || Availability(id, 0x1000) != kOffline || DeleteRequest(id) != 0 || DeleteRequest(id) != kNoRequest) { concurrent_ok = false; }
            }
        });
    }
    for (auto& worker : workers) { worker.join(); }
    check(concurrent_ok && std::all_of(requests.entries.begin(), requests.entries.end(), [](const auto& request) { return !request.id; }),
          "concurrent_create_complete_delete_has_no_stale_aliases_or_leaks");
    std::printf("{\"event\":\"np_request_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
bool CommunityStateSmoke(const std::filesystem::path& path, const std::filesystem::path& directory) {
    using namespace kt::community;
    const auto require = [](bool value) { if (!value) { throw std::runtime_error("Community state fixture failed"); } };
    const auto config = LoadConfig(path); const auto endpoint = ParseEndpoint(config.endpoint);
    require(config.enabled && endpoint.host == L"127.0.0.1" && !endpoint.secure && directory.is_absolute() && !std::filesystem::exists(directory));
    using Initialize = int (KT_SYSV *)(const void*);
    const auto initialize = reinterpret_cast<Initialize>(ResolveUserImport("j3YMu1MVNNo", "libSceUserService", "libSceUserService"));
    require(initialize && initialize(nullptr) == 0);
    require(CommunityScoreModule(CommunityScoreModuleAction::Load) == static_cast<int>(0x805a1001u));
    const auto client = std::make_shared<Client>(config, directory);
    require(ConfigureNativeCommunity(client) && !ConfigureNativeCommunity(client));
    TestCall record; const auto subscription = Register(TestCallback, &record, nullptr);
    require(subscription > 0);
    int state = 73, network = 73, reachable = 73; std::uint64_t account = 73;
    require(GetState(0x1000, &state) == 0 && state == kSignedOut && AccountId(0x1000, &account) == kOffline && account == 73 &&
            NetworkState(&network) == 0 && network == 0 && Reachability(0x1000, &reachable) == 0 && reachable == 0 && Pump() == 0 && record.count == 0);
    const auto pending_availability = CreateRequest();
    require(pending_availability > 0 && Availability(pending_availability, 0x1000) == kOffline && DeleteRequest(pending_availability) == 0);
    require(CommunityScoreModule(CommunityScoreModuleAction::Load) == 0 && CommunityScoreModule(CommunityScoreModuleAction::IsLoaded) == 0);
    std::puts("{\"event\":\"np_community_before_health\",\"ready\":false,\"psn_authenticated\":false}");
    require(std::getchar() == '!'); // The Python fixture holds the real HTTP health response until this barrier.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!client->State().ready && std::chrono::steady_clock::now() < deadline) {
        require(Pump() == 0); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(client->State().ready && Pump() == 0 && Pump() == 0 && record.count == 1 && record.state == kCommunityReady);
    require(GetState(0x1000, &state) == 0 && state == kCommunityReady && AccountId(0x1000, &account) == 0 && account == client->State().account &&
            NetworkState(&network) == 0 && network == 3 && Reachability(0x1000, &reachable) == 0 && reachable == 2);
    const AsyncParameters async_parameters{sizeof(AsyncParameters), 0, 0, {}};
    const auto available = CreateAsyncRequest(&async_parameters); int available_result = 73;
    require(available > 0 && Availability(available, 0x1000) == 0 && PollAsync(available, &available_result) == 0 &&
            available_result == 0 && DeleteRequest(available) == 0);
    using Context = int (KT_SYSV *)(unsigned, int);
    using Delete = int (KT_SYSV *)(int);
    const auto create = reinterpret_cast<Context>(ResolveScoreImport("GWnWQNXZH5M", "libSceNpScore", "libSceNpScore"));
    const auto remove = reinterpret_cast<Delete>(ResolveScoreImport("G0pE+RNCwfk", "libSceNpScore", "libSceNpScore"));
    const auto context = create(0, 0x1000);
    require(context > 0 && CommunityScoreModule(CommunityScoreModuleAction::Unload) == static_cast<int>(0x80020010u));
    require(remove(context) == 0 && CommunityScoreModule(CommunityScoreModuleAction::Unload) == 0 &&
            CommunityScoreModule(CommunityScoreModuleAction::IsLoaded) == static_cast<int>(0x805a1001u));
    client->Stop(); account = 73;
    require(Pump() == 0 && Pump() == 0 && record.count == 2 && record.state == kSignedOut &&
            GetState(0x1000, &state) == 0 && state == kSignedOut && AccountId(0x1000, &account) == kOffline && account == 73 &&
            NetworkState(&network) == 0 && network == 0 && Reachability(0x1000, &reachable) == 0 && reachable == 0 && Unregister(subscription) == 0);
    std::puts("{\"event\":\"np_community_smoke\",\"passed\":true,\"transitions\":2,\"psn_authenticated\":false,\"native_menu_integration\":false}");
    return true;
}
