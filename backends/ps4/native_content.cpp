#include "native_content.h"
#include "native_diagnostics.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
constexpr int kParameter = static_cast<int>(0x80d90002u), kBusy = static_cast<int>(0x80d90003u);
constexpr int kNotFound = static_cast<int>(0x80d90005u), kNoEntitlement = static_cast<int>(0x80d90007u);
constexpr int kNotLoaded = static_cast<int>(0x805a1001u), kModuleBusy = static_cast<int>(0x80020010u);
struct InitParam { std::byte reserved[32]; };
struct BootParam { unsigned reserved1, attributes; std::byte reserved2[32]; };
struct Label { char text[17]; std::byte padding[3]; };
struct Info { Label label; unsigned status; };
struct MountPoint { char path[16]; };
static_assert(sizeof(InitParam) == 32 && sizeof(BootParam) == 40 && offsetof(BootParam, attributes) == 4);
static_assert(sizeof(Label) == 20 && sizeof(Info) == 24 && sizeof(MountPoint) == 16);
struct Mount { unsigned references = 0; };
struct State {
    std::mutex mutex;
    std::shared_ptr<const kt::content::ContentView> view;
    std::array<std::shared_ptr<Mount>, 66> mounts; // Up to 64 installed entries plus two built-in capabilities.
    std::array<bool, 66> key_reported{};
    unsigned references = 0;
    bool initialized = false;
};
State state;
template<typename Function> __declspec(noinline) int Api(const void* caller, Function&& function) {
    try { return function(); }
    catch (const std::bad_alloc&) { UnsupportedApi("native_content_allocation_failure", 0, caller); }
    catch (...) { UnsupportedApi("native_content_host_failure", 0, caller); }
}
void Ready(const void* caller) {
    if (!state.references || !state.initialized) { UnsupportedApi("sceAppContent_requires_initialize", state.references, caller); }
}
std::size_t Find(const Label* label) {
    if (!label) { return SIZE_MAX; }
    const auto end = std::find(std::begin(label->text), std::end(label->text), '\0');
    if (end != label->text + 16) { return SIZE_MAX; }
    const std::string_view name(label->text, 16);
    const auto& addons = state.view->Addons();
    for (std::size_t i = 0; i < addons.size(); ++i) { if (addons[i].label == name) { return i; } }
    return SIZE_MAX;
}
int Initialize(const InitParam* init, BootParam* boot, const void* caller) {
    return Api(caller, [&] {
        std::lock_guard lock(state.mutex);
        if (!state.references) { UnsupportedApi("sceAppContentInitialize_not_loaded", 0, caller); }
        if (!boot || (init && std::any_of(std::begin(init->reserved), std::end(init->reserved), [](auto b) { return b != std::byte{}; }))) { return kParameter; }
        if (state.initialized) { return kBusy; }
        *boot = {}; state.initialized = true;
        std::printf("{\"event\":\"native_content_initialize\",\"title_id\":\"%s\",\"additional_content_count\":%zu}\n", state.view->TitleId().c_str(), state.view->Addons().size());
        return 0;
    });
}
int Parameter(unsigned id, int* output, const void* caller) {
    return Api(caller, [&] {
        std::lock_guard lock(state.mutex);
        if (!state.references) { UnsupportedApi("sceAppContentAppParamGetInt_not_loaded", 0, caller); }
        if (!output || id > 4) { return kParameter; }
        const auto& metadata = state.view->Metadata();
        std::optional<std::int32_t> value;
        if (id == 0) {
            const auto type = metadata.Integer("APP_TYPE");
            if (type == 1) { value = 3; } // Local standalone full application, not an online license claim.
            else { UnsupportedApi("sceAppContentAppParamGetInt_application_type", type ? static_cast<unsigned>(*type) : UINT32_MAX, caller); }
        } else { value = metadata.Integer("USER_DEFINED_PARAM_" + std::to_string(id)); }
        if (!value) { return kParameter; }
        *output = *value;
        std::printf("{\"event\":\"native_content_parameter\",\"id\":%u,\"value\":%d}\n", id, *value); return 0;
    });
}
int List(unsigned service, Info* output, unsigned capacity, unsigned* count, const void* caller) {
    return Api(caller, [&] {
        std::lock_guard lock(state.mutex); Ready(caller);
        if (service) { UnsupportedApi("sceAppContentGetAddcontInfoList_service_label", service, caller); }
        if (capacity > 2500 || ((!output || !capacity) && !count)) { return kParameter; }
        const auto& addons = state.view->Addons();
        if (!output || !capacity) { *count = static_cast<unsigned>(addons.size()); return 0; }
        const auto written = (std::min)(capacity, static_cast<unsigned>(addons.size()));
        for (unsigned i = 0; i < written; ++i) { Info item{}; std::memcpy(item.label.text, addons[i].label.data(), 16); item.status = static_cast<unsigned>(addons[i].status); output[i] = item; }
        if (count) { *count = written; } return 0;
    });
}
int MountAddon(unsigned service, const Label* label, MountPoint* output, const void* caller) {
    return Api(caller, [&] {
        std::lock_guard lock(state.mutex); Ready(caller);
        if (service) { UnsupportedApi("sceAppContentAddcontMount_service_label", service, caller); }
        if (!label || !output) { return kParameter; }
        const auto index = Find(label); if (index == SIZE_MAX) { return kNotFound; }
        if (state.view->Addons()[index].status != kt::content::AddonStatus::Installed) { return kNotFound; }
        auto& mount = state.mounts[index];
        if (!mount) { mount = std::make_shared<Mount>(); }
        if (mount->references == UINT32_MAX) { return kBusy; }
        MountPoint point{}; std::snprintf(point.path, sizeof(point.path), "/addcont%zu", index);
        ++mount->references; *output = point; return 0;
    });
}
int Unmount(const MountPoint* point, const void* caller) {
    return Api(caller, [&] {
        std::lock_guard lock(state.mutex); Ready(caller);
        if (!point || std::find(std::begin(point->path), std::end(point->path), '\0') == std::end(point->path)) { return kParameter; }
        for (std::size_t i = 0; i < state.mounts.size(); ++i) {
            auto& mount = state.mounts[i]; if (!mount || !mount->references) { continue; }
            MountPoint expected{}; std::snprintf(expected.path, sizeof(expected.path), "/addcont%zu", i);
            if (std::strcmp(point->path, expected.path) != 0) { continue; }
            if (mount->references == 1 && mount.use_count() > 1) { return kBusy; }
            --mount->references; return 0;
        }
        return kNotFound;
    });
}
int Key(unsigned service, const Label* label, void* output, const void* caller) {
    return Api(caller, [&] {
        std::lock_guard lock(state.mutex); Ready(caller);
        if (service) { UnsupportedApi("sceAppContentGetEntitlementKey_service_label", service, caller); }
        if (!label || !output) { return kParameter; }
        const auto index = Find(label); if (index == SIZE_MAX) { return kNoEntitlement; }
        std::array<std::byte, 16> key{};
        if (!state.view->EntitlementKey(index, key)) { return kNoEntitlement; }
        std::memcpy(output, key.data(), key.size());
        if (!state.key_reported[index]) {
            state.key_reported[index] = true;
            const auto& item = state.view->Addons()[index];
            std::printf("{\"event\":\"native_content_capability_delivered\",\"label\":\"%s\",\"status\":%u,\"built_in\":%s}\n",
                        item.label.c_str(), static_cast<unsigned>(item.status), item.status == kt::content::AddonStatus::NoExtraData ? "true" : "false");
        }
        return 0;
    });
}
int KT_SYSV ContentInitialize(const InitParam* init, BootParam* boot) { return Initialize(init, boot, __builtin_return_address(0)); }
int KT_SYSV ContentParameter(unsigned id, int* output) { return Parameter(id, output, __builtin_return_address(0)); }
int KT_SYSV ContentList(unsigned service, Info* list, unsigned count, unsigned* written) { return List(service, list, count, written, __builtin_return_address(0)); }
int KT_SYSV ContentMount(unsigned service, const Label* label, MountPoint* output) { return MountAddon(service, label, output, __builtin_return_address(0)); }
int KT_SYSV ContentUnmount(const MountPoint* point) { return Unmount(point, __builtin_return_address(0)); }
int KT_SYSV ContentKey(unsigned service, const Label* label, void* output) { return Key(service, label, output, __builtin_return_address(0)); }
}

void ConfigureNativeContent(const std::filesystem::path& app, const std::filesystem::path& patch, const std::filesystem::path& addons, const std::filesystem::path& user_directory, const std::filesystem::path& read_patches) {
    if (!user_directory.empty()) {
        const auto user = std::filesystem::weakly_canonical(user_directory);
        const auto under = [](const auto& child, const auto& parent) {
            auto c = child.begin();
            for (auto p = parent.begin(); p != parent.end(); ++p, ++c) {
                if (c == child.end() || _wcsicmp(c->c_str(), p->c_str()) != 0) { return false; }
            }
            return true;
        };
        for (const auto& selected : {app, patch, addons}) {
            if (selected.empty()) { continue; }
            const auto root = std::filesystem::weakly_canonical(selected);
            if (under(user, root) || under(root, user)) { throw std::runtime_error("User data and original content directories must be separate"); }
        }
    }
    auto candidate = std::make_shared<kt::content::ContentView>(app, patch, addons, read_patches);
    std::lock_guard lock(state.mutex);
    if (state.view || state.references) { throw std::runtime_error("Content configuration is immutable after setup"); }
    state.view = std::move(candidate);
    std::printf("{\"event\":\"native_content_view\",\"title_id\":\"%s\",\"content_id\":\"%s\",\"additional_content_count\":%zu,\"read_patch_count\":%zu}\n",
                state.view->TitleId().c_str(), state.view->ContentId().c_str(), state.view->Addons().size(), state.view->ReadPatchCount());
    for (const auto& item : state.view->Addons()) {
        std::printf("{\"event\":\"native_content_entry\",\"label\":\"%s\",\"status\":%u,\"built_in\":%s}\n",
                    item.label.c_str(), static_cast<unsigned>(item.status), item.status == kt::content::AddonStatus::NoExtraData ? "true" : "false");
    }
}
bool NativeContentConfigured() { std::lock_guard lock(state.mutex); return state.view != nullptr; }
std::string NativeContentTitleId() { std::lock_guard lock(state.mutex); return state.view ? state.view->TitleId() : std::string{}; }
int ContentModule(ContentModuleAction action, const void* caller) {
    return Api(caller, [&] {
        std::lock_guard lock(state.mutex);
        if (action == ContentModuleAction::IsLoaded) { return state.references ? 0 : kNotLoaded; }
        if (action == ContentModuleAction::Load) {
            if (!state.view) { UnsupportedApi("sceAppContent_content_view_not_configured", 0, caller); }
            if (state.references == UINT32_MAX) { return kBusy; } ++state.references;
        } else {
            if (!state.references) { return kNotLoaded; }
            if (state.references == 1) {
                for (const auto& mount : state.mounts) { if (mount && mount->references) { return kModuleBusy; } }
                state.initialized = false;
            }
            --state.references;
        }
        std::printf("{\"event\":\"native_system_module\",\"operation\":\"%s\",\"id\":180,\"name\":\"libSceAppContentUtil\",\"references\":%u}\n",
                    action == ContentModuleAction::Load ? "load" : "unload", state.references); return 0;
    });
}
std::unique_ptr<kt::content::ReadOnlyFile> OpenNativeContentFile(std::string_view path, bool allow_directory) {
    std::shared_ptr<const kt::content::ContentView> view; std::shared_ptr<Mount> mount;
    std::size_t index = SIZE_MAX, slash = 0;
    {
        std::lock_guard lock(state.mutex); view = state.view;
        if (!view) { throw std::runtime_error("Content view is not configured"); }
        if (!path.starts_with("/app0/") && !(allow_directory && path == "/app0")) {
            for (std::size_t i = 0; i < state.mounts.size(); ++i) {
                const auto root = "/addcont" + std::to_string(i), prefix = root + "/";
                if ((path.starts_with(prefix) || (allow_directory && path == root)) && state.mounts[i] && state.mounts[i]->references) {
                    index = i; slash = (std::min)(path.size(), prefix.size()); mount = state.mounts[i]; break;
                }
            }
            if (index == SIZE_MAX) { throw std::runtime_error("Unknown or unmounted virtual content path"); }
        }
    }
    if (index == SIZE_MAX) { return view->OpenApp0(path.substr((std::min)(path.size(), std::size_t{6})), allow_directory); }
    auto file = view->OpenAddon(index, path.substr(slash), allow_directory); if (file) { file->Hold(mount); } return file;
}
std::uint64_t ResolveContentImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceAppContent" || module != "libSceAppContentUtil") { return 0; }
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "R9lA82OraNs") { return reinterpret_cast<std::uint64_t>(&ContentInitialize); }
    if (id == "99b82IKXpH4") { return reinterpret_cast<std::uint64_t>(&ContentParameter); }
    if (id == "xnd8BJzAxmk") { return reinterpret_cast<std::uint64_t>(&ContentList); }
    if (id == "VANhIWcqYak") { return reinterpret_cast<std::uint64_t>(&ContentMount); }
    if (id == "3rHWaV-1KC4") { return reinterpret_cast<std::uint64_t>(&ContentUnmount); }
    if (id == "XTWR0UXvcgs") { return reinterpret_cast<std::uint64_t>(&ContentKey); }
    return 0;
}
bool ContentSelfTest(const std::filesystem::path& app, const std::filesystem::path& patch, const std::filesystem::path& addons) {
    ConfigureNativeContent(app, patch, addons);
    bool passed = true;
    auto check = [&](bool condition, const char* name) {
        std::printf("{\"event\":\"content_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, condition ? "true" : "false"); passed &= condition;
    };
    check(ContentModule(ContentModuleAction::IsLoaded) == kNotLoaded && ContentModule(ContentModuleAction::Unload) == kNotLoaded,
          "module_not_loaded_before_request");
    check(ContentModule(ContentModuleAction::Load) == 0 && ContentModule(ContentModuleAction::Load) == 0 &&
          ContentModule(ContentModuleAction::Unload) == 0, "module_reference_lifecycle");
    int value = 73;
    check(Parameter(1, &value, nullptr) == 0 && value == 1234 && !state.initialized,
          "patch_integer_available_before_service_initialization");
    check(Parameter(2, &value, nullptr) == 0 && value == -17 && Parameter(0, &value, nullptr) == 0 && value == 3,
          "base_integer_fallback_and_full_application_type");
    value = 73;
    check(Parameter(5, &value, nullptr) == kParameter && value == 73 && Parameter(1, nullptr, nullptr) == kParameter,
          "invalid_parameter_preserves_output");
    InitParam init{};
    struct { BootParam boot; std::uint64_t sentinel = 73; } result;
    std::memset(&result.boot, 0xa5, sizeof(result.boot));
    check(Initialize(&init, &result.boot, nullptr) == 0 && result.sentinel == 73 &&
          std::all_of(reinterpret_cast<const std::byte*>(&result.boot), reinterpret_cast<const std::byte*>(&result.boot) + sizeof(result.boot), [](auto b) { return b == std::byte{}; }) &&
          Initialize(&init, &result.boot, nullptr) == kBusy, "initialization_writes_exactly_40_bytes_and_is_not_repeated");
    auto overlay = OpenNativeContentFile("/app0/overlay.txt");
    std::array<std::byte, 9> bytes{}; bytes.back() = std::byte{73};
    check(overlay && overlay->Size() == 5 && overlay->ReadAt(0, std::span(bytes).first(5)) == 5 &&
          std::memcmp(bytes.data(), "patch", 5) == 0 && bytes.back() == std::byte{73}, "patch_file_wins_without_copying_or_writing_base");
    if (!overlay) { return false; }
    const auto writer = CreateFileW((patch / L"overlay.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    const auto denied = writer == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION;
    if (writer != INVALID_HANDLE_VALUE) { CloseHandle(writer); }
    check(denied, "active_read_only_content_handle_denies_a_writer");
    auto base = OpenNativeContentFile("/app0/fallback.txt");
    check(base && base->Size() == 8 && base->ReadAt(0, std::span(bytes).first(8)) == 8 && std::memcmp(bytes.data(), "fallback", 8) == 0,
          "missing_patch_file_uses_base_file");
    check(overlay->ReadAt(2, std::span(bytes).first(3)) == 3 && std::memcmp(bytes.data(), "tch", 3) == 0 &&
          overlay->ReadAt(5, bytes) == 0 && !OpenNativeContentFile("/app0/absent.bin"), "offset_reads_eof_and_genuine_absence");
    bool rejected = true;
    for (const auto path : {"/app0/../outside", "/app0//file", "/app0/NUL", "/app0/a:stream", "/app0/back\\slash", "/app0/trailing.", "/app0/"}) {
        try { (void)OpenNativeContentFile(path); rejected = false; } catch (const std::runtime_error&) {}
    }
    check(rejected, "traversal_ads_device_names_and_host_aliases_rejected");
    unsigned count = 73;
    check(List(0, nullptr, 0, &count, nullptr) == 0 && count == 2 && List(0, nullptr, 0, nullptr, nullptr) == kParameter,
          "query_reports_only_configured_additional_content");
    struct { Info info; std::uint64_t sentinel = 73; } first{}; first.sentinel = 73;
    check(List(0, &first.info, 1, &count, nullptr) == 0 && count == 1 && first.info.status == 4 && first.sentinel == 73 &&
          std::string_view(first.info.label.text) == "A000000000000001", "bounded_24_byte_list_entry_and_installed_label");
    std::array<std::byte, 17> key{}; key.fill(std::byte{73});
    bool exact = Key(0, &first.info.label, key.data(), nullptr) == 0 && key.back() == std::byte{73};
    for (unsigned i = 0; i < 16; ++i) { exact &= key[i] == static_cast<std::byte>(i + 1); }
    check(exact, "key_is_read_from_exact_local_input_without_extra_output");
    Label second{}; std::memcpy(second.text, "A000000000000002", 16); key.fill(std::byte{73});
    check(Key(0, &second, key.data(), nullptr) == kNoEntitlement && std::all_of(key.begin(), key.end(), [](auto b) { return b == std::byte{73}; }),
          "missing_key_is_not_fabricated");
    MountPoint point{};
    check(MountAddon(0, &first.info.label, &point, nullptr) == 0 && std::string_view(point.path) == "/addcont0" &&
          ContentModule(ContentModuleAction::Unload) == kModuleBusy, "mount_registers_real_addon_view_and_pins_module");
    auto payload = OpenNativeContentFile("/addcont0/payload.txt");
    check(payload && payload->ReadAt(0, std::span(bytes).first(5)) == 5 && std::memcmp(bytes.data(), "addon", 5) == 0 &&
          Unmount(&point, nullptr) == kBusy, "open_addon_file_pins_mount_until_handle_closes");
    payload.reset();
    check(Unmount(&point, nullptr) == 0 && Unmount(&point, nullptr) == kNotFound, "unmount_after_last_open_file_and_stale_mount_error");
    bool closed = false;
    try { (void)OpenNativeContentFile("/addcont0/payload.txt"); } catch (const std::runtime_error&) { closed = true; }
    check(closed && ContentModule(ContentModuleAction::Unload) == 0 && ContentModule(ContentModuleAction::IsLoaded) == kNotLoaded,
          "unmounted_virtual_path_is_unavailable_and_module_unloads");
    std::printf("{\"event\":\"content_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
