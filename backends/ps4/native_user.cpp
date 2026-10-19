#include "native_user.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
constexpr int Error(unsigned code) { return static_cast<int>(0x80960000u | code); }
constexpr int kNotInitialized = Error(2), kAlreadyInitialized = Error(3), kInvalid = Error(5);
constexpr int kNoEvent = Error(7), kNotLoggedIn = Error(9), kShortBuffer = Error(10);
// A development-local player slot, not a Windows identity or an online account.
// Save/profile mapping must keep this ID stable when the display name changes.
constexpr int kLocalUser = 0x1000;
constexpr char kLocalName[] = "Player 1";
struct Event { int type, user; };
struct InitializeParam { int priority; };
std::mutex gate;
bool initialized{}, pending_login{};

int KT_SYSV Initialize(const InitializeParam* param) {
    std::lock_guard lock(gate);
    if (initialized) { return kAlreadyInitialized; }
    if (param && (param->priority < 0x100 || param->priority > 0x2ff)) { return kInvalid; }
    initialized = true;
    pending_login = true;
    std::printf("{\"event\":\"native_local_user\",\"operation\":\"initialize\",\"user_id\":%d,\"online\":false}\n", kLocalUser);
    return 0;
}
int KT_SYSV Terminate() {
    std::lock_guard lock(gate);
    if (!initialized) { return kNotInitialized; }
    initialized = false;
    pending_login = false;
    return 0;
}
int KT_SYSV InitialUser(int* user) {
    std::lock_guard lock(gate);
    if (!initialized) { return kNotInitialized; }
    if (!user) { return kInvalid; }
    *user = kLocalUser;
    return 0;
}
int KT_SYSV LoginUsers(std::array<int, 4>* users) {
    std::lock_guard lock(gate);
    if (!initialized) { return kNotInitialized; }
    if (!users) { return kInvalid; }
    *users = {kLocalUser, -1, -1, -1};
    return 0;
}
int KT_SYSV GetEvent(Event* event) {
    std::lock_guard lock(gate);
    if (!initialized) { return kNotInitialized; }
    if (!event) { return kInvalid; }
    if (!pending_login) { return kNoEvent; }
    *event = {0, kLocalUser};
    pending_login = false;
    return 0;
}
int KT_SYSV UserName(int user, char* output, std::size_t size) {
    std::lock_guard lock(gate);
    if (!initialized) { return kNotInitialized; }
    if (!output || user == -1) { return kInvalid; }
    if (user != kLocalUser) { return kNotLoggedIn; }
    if (size < sizeof(kLocalName)) { return kShortBuffer; }
    std::memcpy(output, kLocalName, sizeof(kLocalName));
    return 0;
}
} // namespace

bool NativeUserInitialized() { std::lock_guard lock(gate); return initialized; }

std::uint64_t ResolveUserImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceUserService" || module != library) { return 0; }
    const auto hash = identifier.substr(0, identifier.find('#'));
    struct Binding { std::string_view hash; std::uint64_t address; };
    const Binding bindings[]{
        {"j3YMu1MVNNo", reinterpret_cast<std::uint64_t>(&Initialize)},
        {"bwFjS+bX9mA", reinterpret_cast<std::uint64_t>(&Terminate)},
        {"CdWp0oHWGr0", reinterpret_cast<std::uint64_t>(&InitialUser)},
        {"fPhymKNvK-A", reinterpret_cast<std::uint64_t>(&LoginUsers)},
        {"yH17Q6NWtVg", reinterpret_cast<std::uint64_t>(&GetEvent)},
        {"1xxcMiGu2fo", reinterpret_cast<std::uint64_t>(&UserName)},
    };
    for (const auto& binding : bindings) {
        if (hash == binding.hash) { return binding.address; }
    }
    return 0;
}

bool UserSelfTest() {
    bool passed = true;
    const auto check = [&](const char* name, bool result) {
        std::printf("{\"event\":\"user_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
        passed &= result;
    };
    int user = -1;
    std::array<int, 4> users{};
    Event event{-1, -1};
    char name[32]{};
    check("not_initialized", InitialUser(&user) == kNotInitialized && LoginUsers(&users) == kNotInitialized &&
          GetEvent(&event) == kNotInitialized && UserName(kLocalUser, name, sizeof(name)) == kNotInitialized && Terminate() == kNotInitialized);
    InitializeParam bad_priority{0};
    check("initialize", Initialize(&bad_priority) == kInvalid && Initialize(nullptr) == 0 && Initialize(nullptr) == kAlreadyInitialized);
    check("initial_and_list", InitialUser(&user) == 0 && user == kLocalUser && LoginUsers(&users) == 0 && users == std::array<int, 4>{kLocalUser, -1, -1, -1});
    check("null_arguments", InitialUser(nullptr) == kInvalid && LoginUsers(nullptr) == kInvalid && GetEvent(nullptr) == kInvalid && UserName(user, nullptr, 32) == kInvalid);
    check("event_consumption", GetEvent(&event) == 0 && event.type == 0 && event.user == user && GetEvent(&event) == kNoEvent);
    name[0] = '!';
    check("name_buffer", UserName(user, name, 1) == kShortBuffer && name[0] == '!' &&
          UserName(-1, name, sizeof(name)) == kInvalid && UserName(user + 1, name, sizeof(name)) == kNotLoggedIn &&
          UserName(user, name, sizeof(name)) == 0 && std::strcmp(name, kLocalName) == 0);
    check("terminate_and_reinitialize", Terminate() == 0 && InitialUser(&user) == kNotInitialized &&
          Initialize(nullptr) == 0 && InitialUser(&user) == 0 && user == kLocalUser && GetEvent(&event) == 0 && Terminate() == 0);
    std::printf("{\"event\":\"user_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
