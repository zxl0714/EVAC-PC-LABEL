#include "native_storage.h"
#include "native_memory.h"
#include "native_diagnostics.h"
#include "native_user.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <array>
#include <vector>
#include <limits>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
constexpr int kUserNotInitialized = static_cast<int>(0x80960002u);
constexpr int kParameter = static_cast<int>(0x809f0000u);
constexpr int kNotInitialized = static_cast<int>(0x809f0001u);
constexpr int kNotFound = static_cast<int>(0x809f0008u);
constexpr int kInternal = static_cast<int>(0x809f000bu);
constexpr int kInvalidUser = static_cast<int>(0x809f0011u);
constexpr int kBusy = static_cast<int>(0x809f0003u), kNotMounted = static_cast<int>(0x809f0004u);
constexpr int kExists = static_cast<int>(0x809f0007u), kNoSpace = static_cast<int>(0x809f000au), kBroken = static_cast<int>(0x809f000fu);
std::mutex gate;
std::filesystem::path profile_root;
bool initialized{};
std::unique_ptr<kt::save::Store> save_store;
std::array<std::shared_ptr<kt::save::Mount>, 16> mounts;

struct DirectoryName { char value[32]; };
struct MountRequest {
    int user, padding;
    const DirectoryName* directory;
    std::uint64_t blocks;
    std::uint32_t mode;
    unsigned char reserved[32];
    std::uint32_t padding2;
};
struct MountResult {
    char point[16];
    std::uint64_t required_blocks;
    std::uint32_t unused, status;
    unsigned char reserved[28];
    std::uint32_t padding;
};
static_assert(sizeof(MountRequest) == 64 && offsetof(MountRequest, mode) == 24);
static_assert(sizeof(MountResult) == 64 && offsetof(MountResult, status) == 28);
struct TitleId { char value[10], padding[6]; };
struct MountPoint { char value[16]; };
struct SearchCondition { int user, padding; const TitleId* title; const DirectoryName* pattern; unsigned key, order; unsigned char reserved[32]; };
struct SaveParam { char title[128], subtitle[128], detail[1024]; unsigned user, padding; std::int64_t modified; unsigned char reserved[32]; };
struct SearchInfo { std::uint64_t blocks, free_blocks; unsigned char reserved[32]; };
struct SearchResult { unsigned hits, padding; DirectoryName* names; unsigned capacity, set; SaveParam* params; SearchInfo* infos; unsigned char reserved[12]; unsigned padding2; };
struct Icon { const void* buffer; std::size_t capacity, size; unsigned char reserved[32]; };
struct DeletePrefix { int user, padding; const TitleId* title; const DirectoryName* directory; const void* fingerprint; };
static_assert(sizeof(SearchCondition) == 64 && offsetof(SearchCondition, key) == 24);
static_assert(sizeof(SearchResult) == 56 && offsetof(SearchResult, set) == 20 && offsetof(SearchResult, infos) == 32);
static_assert(sizeof(SaveParam) == 1328 && offsetof(SaveParam, modified) == 1288 && sizeof(SearchInfo) == 48 && sizeof(Icon) == 56);

int SaveError(kt::save::Error error) {
    using E = kt::save::Error;
    switch (error) {
    case E::Invalid: case E::BadFile: case E::Directory: case E::NotDirectory: return kParameter;
    case E::ReadOnly: return static_cast<int>(0x809f000du);
    case E::NotFound: return kNotFound;
    case E::Exists: return kExists;
    case E::Busy: return kBusy;
    case E::NotMounted: return kNotMounted;
    case E::NoSpace: return kNoSpace;
    case E::Corrupt: return kBroken;
    default: return kInternal;
    }
}
template<typename Function> __declspec(noinline) int StorageApi(Function&& function) noexcept {
    try { return function(); }
    catch (const kt::save::Failure& error) { std::fprintf(stderr, "Native save operation: %s (win32=%u)\n", error.what(), static_cast<unsigned>(error.native_error)); return SaveError(error.error); }
    catch (const std::bad_alloc&) { return static_cast<int>(0x809f0002u); }
    catch (const std::exception& error) { std::fprintf(stderr, "Native save operation: %s\n", error.what()); return kInternal; }
    catch (...) { return kInternal; }
}
bool ReadGuest(const void* input, void* output, std::size_t size) {
    SIZE_T read{}; return input && ReadProcessMemory(GetCurrentProcess(), input, output, size, &read) && read == size;
}
bool Writable(void* output, std::size_t bytes) {
    return NativeMemoryWritable(output, bytes);
}
template<std::size_t Size> bool Text(const char (&input)[Size], std::string& result) {
    const auto end = std::find(input, input + Size, '\0'); if (end == input + Size) { return false; }
    result.assign(input, end); return true;
}
bool CurrentTitle(const TitleId* title) {
    if (!title) { return true; } TitleId value{}; std::string name;
    return ReadGuest(title, &value, sizeof(value)) && Text(value.value, name) && name == "CUSA09062";
}
bool PointSlot(std::string_view point, unsigned& slot, std::string_view& relative) {
    constexpr std::string_view prefix = "/savedata";
    if (!point.starts_with(prefix)) { return false; }
    const auto slash = point.find('/', prefix.size()); const auto number = point.substr(prefix.size(), slash == std::string_view::npos ? point.size() - prefix.size() : slash - prefix.size());
    if (number.empty() || number.size() > 2 || (number.size() > 1 && number[0] == '0')) { return false; }
    slot = 0; for (const auto c : number) { if (c < '0' || c > '9') { return false; } slot = slot * 10 + static_cast<unsigned>(c - '0'); }
    relative = slash == std::string_view::npos ? std::string_view{} : point.substr(slash + 1); return slot < mounts.size();
}
std::shared_ptr<kt::save::Mount> Mounted(const MountPoint* point) {
    MountPoint value{}; std::string path; unsigned slot{}; std::string_view relative;
    if (!ReadGuest(point, &value, sizeof(value)) || !Text(value.value, path) || !PointSlot(path, slot, relative) || !relative.empty()) { return {}; }
    return mounts[slot];
}

bool DirectoryToken(const DirectoryName& name, std::string& token) {
    std::size_t size{};
    while (size < sizeof(name.value) && name.value[size]) { ++size; }
    if (!size || size == sizeof(name.value)) { return false; }
    token.assign(name.value, size);
    // A save name is one virtual component, never a host path. A deliberately
    // narrow ASCII namespace also avoids Windows aliases and normalization.
    for (const auto c : token) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) { return false; }
    }
    auto upper = token;
    for (auto& c : upper) { if (c >= 'a' && c <= 'z') { c -= 'a' - 'A'; } }
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
        (upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) && upper[3] >= '1' && upper[3] <= '9')) { return false; }
    return true;
}

bool Under(const std::filesystem::path& path, const std::filesystem::path& parent) {
    auto part = path.begin();
    for (const auto& expected : parent) {
        if (part == path.end() || CompareStringOrdinal(part->c_str(), -1, expected.c_str(), -1, TRUE) != CSTR_EQUAL) { return false; }
        ++part;
    }
    return true;
}

std::filesystem::path CheckedRoot(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    if (!directory.is_absolute()) { throw std::runtime_error("The native user directory must be absolute"); }
    const auto root = fs::weakly_canonical(directory);
    const auto workspace = fs::weakly_canonical(fs::current_path());
    if (root == root.root_path() || Under(workspace, root)) {
        throw std::runtime_error("The native user directory must be separate from the workspace root and original game data");
    }
    wchar_t executable[32768]{};
    const auto count = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (!count || count == std::size(executable)) { throw std::runtime_error("Cannot resolve the native executable location"); }
    // Archived probes run from reports/<run>/binary, not necessarily the project
    // working directory. Protect game-data beside ANY executable/cwd ancestor.
    for (auto ancestor : {workspace, fs::path(executable).parent_path()}) {
        while (!ancestor.empty()) {
            const auto original = fs::weakly_canonical(ancestor / "game-data");
            if (Under(root, original) || Under(original, root)) {
                throw std::runtime_error("The native user directory must be separate from original game data");
            }
            const auto parent = ancestor.parent_path();
            if (parent == ancestor) { break; }
            ancestor = parent;
        }
    }
    if (fs::exists(root) && !fs::is_directory(root)) {
        throw std::runtime_error("The native user directory is not a directory");
    }
    // Reject links in the supplied path as well as checking the canonical path.
    // This service creates only its own save/profile hierarchy, never game files.
    auto cursor = directory.lexically_normal();
    while (!cursor.empty()) {
        const auto attributes = GetFileAttributesW(cursor.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            throw std::runtime_error("Reparse points are not supported in the native user directory");
        }
        const auto parent = cursor.parent_path();
        if (parent == cursor) { break; }
        cursor = parent;
    }
    return root;
}

void CheckWritable(const std::filesystem::path& directory) {
    static std::atomic<unsigned> serial{};
    const auto probe = directory / (L".write-check-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial));
    const auto file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) { throw std::runtime_error("Native save directory is not writable"); }
    constexpr char marker[] = "Ketsui native storage check";
    DWORD written{};
    const bool success = WriteFile(file, marker, sizeof(marker), &written, nullptr) && written == sizeof(marker) && FlushFileBuffers(file);
    CloseHandle(file); // deletes only this newly created scratch file
    if (!success) { throw std::runtime_error("Native save directory failed its write/flush check"); }
}

__declspec(noinline) int InitializeHost(void* reserved, const void* caller) {
    std::lock_guard lock(gate);
    if (reserved) { return kParameter; }
    if (!NativeUserInitialized()) { return kUserNotInitialized; }
    if (initialized) { return 0; }
    if (profile_root.empty()) { UnsupportedApi("sceSaveDataInitialize3_without_user_directory", 0, caller); }
    try {
        const auto checked = CheckedRoot(profile_root);
        const auto saves = checked / "profiles" / "1000" / "CUSA09062" / "savedata";
        (void)CheckedRoot(saves);
        std::filesystem::create_directories(saves);
        // Recheck after creation, including any newly materialized components.
        (void)CheckedRoot(saves);
        CheckWritable(saves);
        auto candidate = std::make_unique<kt::save::Store>(saves);
        save_store = std::move(candidate);
        initialized = true;
        std::puts("{\"event\":\"native_storage_initialized\",\"profile\":\"1000\",\"title\":\"CUSA09062\",\"mounts\":0}");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Native storage initialization failed: %s\n", error.what());
        return kInternal;
    }
}
int KT_SYSV Initialize(void* reserved) { return InitializeHost(reserved, __builtin_return_address(0)); }

int KT_SYSV Terminate() {
    std::lock_guard lock(gate);
    if (!initialized) { return kNotInitialized; }
    if (save_store && save_store->HasMounts()) { return kBusy; }
    save_store.reset();
    initialized = false;
    return 0;
}

// Keep Windows exceptions and filesystem objects out of the SysV boundary.
__declspec(noinline) int MountHost(const MountRequest* request, MountResult* result, const void* caller) noexcept {
    std::string name; unsigned mode{};
    const auto status = StorageApi([&] {
        std::lock_guard lock(gate);
        if (!initialized) { return kNotInitialized; }
        MountRequest input{}; DirectoryName directory{};
        if (!ReadGuest(request, &input, sizeof(input)) || !Writable(result, sizeof(*result)) || !ReadGuest(input.directory, &directory, sizeof(directory))) { return kParameter; }
        if (input.user != 0x1000) { return kInvalidUser; }
        mode = input.mode;
        if (!DirectoryToken(directory, name) || !(input.mode & 3) || (input.mode & 3) == 3 || input.mode & ~0x3fu) { return kParameter; }
        if (input.mode != 1 && input.mode != 2 && input.mode != 6 && input.mode != 34) { UnsupportedApi("sceSaveDataMount2_mode", input.mode, caller); }
        const auto free = std::find(mounts.begin(), mounts.end(), nullptr); if (free == mounts.end()) { return static_cast<int>(0x809f000cu); }
        bool created{}; auto mount = save_store->MountSlot(name, (input.mode & 2) != 0, (input.mode & 36) != 0, (input.mode & 4) != 0, input.blocks, created);
        MountResult completed{}; const auto slot = static_cast<unsigned>(free - mounts.begin());
        std::snprintf(completed.point, sizeof(completed.point), "/savedata%u", slot); completed.status = created ? 1 : 0;
        *free = std::move(mount); std::memcpy(result, &completed, sizeof(completed));
        std::printf("{\"event\":\"native_storage_mount\",\"directory\":\"%s\",\"mode\":%u,\"slot\":%u,\"created\":%s,\"result\":0}\n", name.c_str(), input.mode, slot, created ? "true" : "false");
        return 0;
    });
    if (status == kNotFound) { std::printf("{\"event\":\"native_storage_mount\",\"directory\":\"%s\",\"mode\":%u,\"result\":\"not_found\",\"output_modified\":false}\n", name.c_str(), mode); }
    return status;
}
int KT_SYSV Mount2(const MountRequest* request, MountResult* result) {
    return MountHost(request, result, __builtin_return_address(0));
}
__declspec(noinline) int SearchHost(const SearchCondition* condition, SearchResult* output) noexcept {
    return StorageApi([&] {
        std::lock_guard lock(gate); if (!initialized) { return kNotInitialized; }
        SearchCondition request{}; SearchResult result{};
        if (!ReadGuest(condition, &request, sizeof(request)) || !ReadGuest(output, &result, sizeof(result)) || !Writable(output, sizeof(*output))) { return kParameter; }
        if (request.user != 0x1000) { return kInvalidUser; }
        if (!CurrentTitle(request.title) || (request.key > 3 && request.key != 5) || request.order > 1 || (result.capacity && !result.names)) { return kParameter; }
        std::string pattern;
        if (request.pattern) {
            DirectoryName name{}; if (!ReadGuest(request.pattern, &name, sizeof(name)) || !Text(name.value, pattern)) { return kParameter; }
            for (const unsigned char c : pattern) { if (c < 32 || c > 126 || std::string_view("/\\:*?\"<>|").find(static_cast<char>(c)) != std::string_view::npos) { return kParameter; } }
        }
        const auto found = save_store->Search(pattern, static_cast<kt::save::SortKey>(request.key), request.order != 0);
        const auto count = (std::min)(static_cast<std::size_t>(result.capacity), found.size());
        if (!Writable(result.names, count * sizeof(DirectoryName)) || (result.params && !Writable(result.params, count * sizeof(SaveParam))) ||
            (result.infos && !Writable(result.infos, count * sizeof(SearchInfo)))) { return kParameter; }
        std::vector<DirectoryName> names(count); std::vector<SaveParam> params(result.params ? count : 0); std::vector<SearchInfo> infos(result.infos ? count : 0);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& entry = found[index]; std::memcpy(names[index].value, entry.name.data(), entry.name.size());
            if (result.params) {
                auto& target = params[index]; std::memcpy(target.title, entry.params.title.data(), entry.params.title.size());
                std::memcpy(target.subtitle, entry.params.subtitle.data(), entry.params.subtitle.size()); std::memcpy(target.detail, entry.params.detail.data(), entry.params.detail.size());
                target.user = entry.params.user; target.modified = entry.params.modified;
            }
            if (result.infos) { infos[index].blocks = entry.blocks; infos[index].free_blocks = entry.free_blocks; }
        }
        if (count) {
            std::memcpy(result.names, names.data(), count * sizeof(DirectoryName));
            if (result.params) { std::memcpy(result.params, params.data(), count * sizeof(SaveParam)); }
            if (result.infos) { for (std::size_t index = 0; index < count; ++index) { std::memcpy(&result.infos[index], &infos[index], 16); } }
        }
        const auto hits = static_cast<unsigned>(found.size()), set = static_cast<unsigned>(count);
        std::memcpy(&output->hits, &hits, sizeof(hits)); std::memcpy(&output->set, &set, sizeof(set));
        std::printf("{\"event\":\"native_storage_search\",\"pattern_bytes\":%zu,\"key\":%u,\"order\":%u,\"hits\":%u,\"set\":%u,\"capacity\":%u}\n", pattern.size(), request.key, request.order, hits, set, result.capacity);
        return 0;
    });
}
int KT_SYSV DirectorySearch(const SearchCondition* condition, SearchResult* result) { return SearchHost(condition, result); }
__declspec(noinline) int UnmountHost(const MountPoint* point) noexcept {
    return StorageApi([&] {
        std::lock_guard lock(gate); if (!initialized) { return kNotInitialized; }
        if (!point) { return kParameter; } const auto mount = Mounted(point); if (!mount) { return kNotMounted; }
        save_store->Unmount(mount);
        for (auto& item : mounts) { if (item == mount) { item.reset(); break; } }
        std::printf("{\"event\":\"native_storage_unmount\",\"directory\":\"%s\",\"flushed\":true}\n", mount->Name().c_str()); return 0;
    });
}
int KT_SYSV Unmount(const MountPoint* point) { return UnmountHost(point); }
__declspec(noinline) int SetParamHost(const MountPoint* point, unsigned type, const void* data, std::size_t size) noexcept {
    return StorageApi([&] {
        std::lock_guard lock(gate); if (!initialized) { return kNotInitialized; }
        if (!point || !data || type > 4) { return kParameter; } const auto mount = Mounted(point); if (!mount) { return kNotMounted; }
        auto params = save_store->Describe(mount).params;
        if (!type) {
            SaveParam value{};
            if (size != sizeof(value) || !ReadGuest(data, &value, sizeof(value)) || !Text(value.title, params.title) || !Text(value.subtitle, params.subtitle) || !Text(value.detail, params.detail)) { return kParameter; }
            params.user = value.user;
        } else if (type == 4) { if (size != 4 || !ReadGuest(data, &params.user, 4)) { return kParameter; } }
        else {
            const auto bound = type == 3 ? 1024u : 128u; if (!size || size > bound) { return kParameter; }
            std::array<char, 1024> value{}; if (!ReadGuest(data, value.data(), size) || std::find(value.begin(), value.begin() + size, '\0') == value.begin() + size) { return kParameter; }
            std::string text(value.data()); if (type == 1) { params.title = std::move(text); } else if (type == 2) { params.subtitle = std::move(text); } else { params.detail = std::move(text); }
        }
        save_store->SetParams(mount, params);
        std::printf("{\"event\":\"native_storage_set_param\",\"directory\":\"%s\",\"type\":%u,\"bytes\":%zu,\"staged\":true,\"committed\":false}\n", mount->Name().c_str(), type, size); return 0;
    });
}
int KT_SYSV SetParam(const MountPoint* point, unsigned type, const void* data, std::size_t size) { return SetParamHost(point, type, data, size); }
__declspec(noinline) int SaveIconHost(const MountPoint* point, const Icon* input) noexcept {
    return StorageApi([&] {
        std::lock_guard lock(gate); if (!initialized) { return kNotInitialized; }
        Icon request{}; if (!point || !ReadGuest(input, &request, sizeof(request)) || !request.buffer) { return kParameter; }
        const auto mount = Mounted(point); if (!mount) { return kNotMounted; }
        const auto count = (std::min)(request.capacity, request.size); if (!count || count > 16 * 1024 * 1024) { return kParameter; }
        std::vector<std::byte> data(count); if (!ReadGuest(request.buffer, data.data(), data.size())) { return kParameter; }
        save_store->SaveIcon(mount, data);
        std::printf("{\"event\":\"native_storage_save_icon\",\"directory\":\"%s\",\"bytes\":%zu,\"staged\":true,\"committed\":false}\n", mount->Name().c_str(), count); return 0;
    });
}
int KT_SYSV SaveIcon(const MountPoint* point, const Icon* input) { return SaveIconHost(point, input); }
__declspec(noinline) int DeleteHost(const DeletePrefix* input, const void* caller) noexcept {
    return StorageApi([&] {
        std::lock_guard lock(gate); if (!initialized) { return kNotInitialized; }
        DeletePrefix request{}; DirectoryName directory{}; std::string name;
        if (!ReadGuest(input, &request, sizeof(request)) || !CurrentTitle(request.title) || !ReadGuest(request.directory, &directory, sizeof(directory)) || !DirectoryToken(directory, name)) { return kParameter; }
        if (request.user != 0x1000) { return kInvalidUser; }
        if (request.fingerprint) { UnsupportedApi("sceSaveDataDelete_fingerprint", 0, caller); }
        save_store->Delete(name);
        std::printf("{\"event\":\"native_storage_delete\",\"directory\":\"%s\",\"recoverable\":true,\"recovery_directory\":\".trash\"}\n", name.c_str()); return 0;
    });
}
int KT_SYSV Delete(const DeletePrefix* input) { return DeleteHost(input, __builtin_return_address(0)); }
} // namespace

bool NativeSavePath(std::string_view path) { return path.starts_with("/savedata"); }
std::unique_ptr<kt::save::File> OpenNativeSaveFile(std::string_view path, kt::save::OpenOptions options) {
    std::lock_guard lock(gate); unsigned slot{}; std::string_view relative;
    if (!PointSlot(path, slot, relative)) { throw kt::save::Failure(kt::save::Error::Invalid, "Invalid mounted save path"); }
    if (!initialized || !save_store || !mounts[slot]) { throw kt::save::Failure(kt::save::Error::NotMounted, "Save mount does not exist"); }
    return save_store->Open(mounts[slot], relative, options);
}

void ConfigureNativeStorage(const std::filesystem::path& directory) {
    std::lock_guard lock(gate);
    if (initialized) { throw std::runtime_error("Cannot change the user directory while storage is initialized"); }
    profile_root = CheckedRoot(directory);
}
std::filesystem::path NativeStorageRoot() { std::lock_guard lock(gate); return profile_root; }

std::uint64_t ResolveStorageImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceSaveData" || module != library) { return 0; }
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (hash == "TywrFKCoLGY") { return reinterpret_cast<std::uint64_t>(&Initialize); }
    if (hash == "yKDy8S5yLA0") { return reinterpret_cast<std::uint64_t>(&Terminate); }
    if (hash == "0z45PIH+SNI") { return reinterpret_cast<std::uint64_t>(&Mount2); }
    if (hash == "dyIhnXq-0SM") { return reinterpret_cast<std::uint64_t>(&DirectorySearch); }
    if (hash == "BMR4F-Uek3E") { return reinterpret_cast<std::uint64_t>(&Unmount); }
    if (hash == "85zul--eGXs") { return reinterpret_cast<std::uint64_t>(&SetParam); }
    if (hash == "c88Yy54Mx0w") { return reinterpret_cast<std::uint64_t>(&SaveIcon); }
    if (hash == "S1GkePI17zQ") { return reinterpret_cast<std::uint64_t>(&Delete); }
    return 0;
}

bool StorageSelfTest(const std::filesystem::path& directory) {
    bool passed = true;
    const auto check = [&](const char* name, bool result) {
        std::printf("{\"event\":\"storage_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
        passed &= result;
    };
    ConfigureNativeStorage(directory);
    check("mount_requires_initialization", Mount2(nullptr, nullptr) == kNotInitialized);
    check("user_required", Initialize(nullptr) == kUserNotInitialized);
    const auto address = ResolveUserImport("j3YMu1MVNNo#A#A", "libSceUserService", "libSceUserService");
    const auto user_initialize = reinterpret_cast<int(KT_SYSV*)(const void*)>(address);
    check("user_initialize", user_initialize(nullptr) == 0);
    check("reserved_parameter", Initialize(reinterpret_cast<void*>(1)) == kParameter);
    check("initialize_and_repeat", Initialize(nullptr) == 0 && Initialize(nullptr) == 0);
    const auto saves = directory / "profiles" / "1000" / "CUSA09062" / "savedata";
    check("backing_directory", std::filesystem::is_directory(saves));
    MountResult result; std::memset(&result, 0xa5, sizeof(result));
    DirectoryName name{"KT_MISSING_SYNTHETIC"};
    MountRequest request{0x1000, 0, &name, 0, 1, {}, 0};
    check("mount_null_arguments", Mount2(nullptr, &result) == kParameter && Mount2(&request, nullptr) == kParameter);
    request.user = 0x1001;
    check("mount_requires_local_user", Mount2(&request, &result) == kInvalidUser);
    request.user = 0x1000;
    const auto original_result = result;
    check("missing_read_mount_does_not_change_output", Mount2(&request, &result) == kNotFound &&
          std::memcmp(&result, &original_result, sizeof(result)) == 0 && !std::filesystem::exists(saves / name.value));
    name = DirectoryName{"../escape"};
    check("mount_rejects_traversal", Mount2(&request, &result) == kParameter);
    name = DirectoryName{"cOn"};
    check("mount_rejects_windows_aliases", Mount2(&request, &result) == kParameter);
    std::memset(name.value, 'a', sizeof(name.value));
    check("mount_requires_bounded_directory_name", Mount2(&request, &result) == kParameter);
    name = DirectoryName{"KT_MISSING_SYNTHETIC"}; request.mode = 3;
    check("mount_rejects_conflicting_access", Mount2(&request, &result) == kParameter);
    check("terminate", Terminate() == 0 && Terminate() == kNotInitialized);
    check("reinitialize", Initialize(nullptr) == 0 && Terminate() == 0 && std::filesystem::is_directory(saves));
    bool rejected = false;
    try { ConfigureNativeStorage(std::filesystem::current_path() / "game-data" / "forbidden-test"); }
    catch (const std::exception&) { rejected = true; }
    check("original_data_protected", rejected);
    std::printf("{\"event\":\"storage_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}

bool SaveStoreSelfTest(const std::filesystem::path& directory) {
    namespace fs = std::filesystem; using E = kt::save::Error;
    ConfigureNativeStorage(directory);
    if (!fs::exists(directory) || !fs::is_empty(directory)) { throw std::runtime_error("Save-store self-test requires a fresh empty user directory"); }
    bool passed = true; unsigned cases{};
    const auto check = [&](const char* name, bool result) {
        ++cases; passed &= result; std::printf("{\"event\":\"save_store_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    const auto rejects = [](E expected, auto function) { try { function(); return false; } catch (const kt::save::Failure& error) { return error.error == expected; } };
    const auto user_initialize = reinterpret_cast<int(KT_SYSV*)(const void*)>(ResolveUserImport("j3YMu1MVNNo#A#A", "libSceUserService", "libSceUserService"));
    check("fresh_store_initializes_local_profile", user_initialize(nullptr) == 0 && Initialize(nullptr) == 0);
    auto& store = *save_store; const auto root = directory / "profiles" / "1000" / "CUSA09062" / "savedata";
    check("fresh_store_has_no_fabricated_slots", store.Search("%", kt::save::SortKey::Name, false).empty());
    bool created{};
    check("missing_mount_does_not_create_data", rejects(E::NotFound, [&] { (void)store.MountSlot("Missing", false, false, false, 0, created); }) && !fs::exists(root / "Missing"));
    check("slot_namespace_rejects_traversal_and_device_names", rejects(E::Invalid, [&] { (void)store.MountSlot("../escape", true, true, true, 96, created); }) &&
        rejects(E::Invalid, [&] { (void)store.MountSlot("CON", true, true, true, 96, created); }));
    auto mount = store.MountSlot("SlotA", true, true, true, 96, created);
    check("creation_stages_metadata_and_real_quota_until_unmount", created && !fs::exists(root / "SlotA") && store.Describe(mount).blocks == 96);
    check("active_mount_prevents_duplicate_mount_and_termination", rejects(E::Busy, [&] { (void)store.MountSlot("slota", false, false, false, 0, created); }) && Terminate() == kBusy);
    auto file = store.Open(mount, "data.bin", {.read = true, .write = true, .create = true, .truncate = true});
    const std::array<std::byte, 7> payload{std::byte{}, std::byte{1}, std::byte{127}, std::byte{128}, std::byte{255}, std::byte{17}, std::byte{73}};
    std::uint64_t end{}; std::array<std::byte, 12> readback{};
    check("save_file_writes_actual_bytes_and_updates_size", file->WriteAt(0, payload, end) == payload.size() && end == payload.size() && file->Info().size == payload.size());
    readback.fill(std::byte{91});
    check("save_file_offset_read_preserves_tail_and_eof", file->ReadAt(2, readback) == 5 && std::equal(payload.begin() + 2, payload.end(), readback.begin()) && readback[5] == std::byte{91} && file->ReadAt(1000, readback) == 0);
    check("zero_write_does_not_extend_a_file", file->WriteAt(1ull << 32, {}, end) == 0 && file->Info().size == payload.size());
    auto read_create = store.Open(mount, "empty.bin", {.read = true, .create = true});
    read_create->Flush();
    check("readonly_create_flushes_creation_without_granting_guest_writes", read_create->Info().size == 0 &&
        rejects(E::BadFile, [&] { (void)read_create->WriteAt(0, payload, end); })); read_create.reset();
    check("quota_failure_is_detected_before_data_changes", rejects(E::NoSpace, [&] { (void)file->WriteAt(96 * 32768, std::span(payload).first(1), end); }) && file->Info().size == payload.size());
    check("open_files_pin_mount_and_prevent_removal", rejects(E::Busy, [&] { store.Unmount(mount); }) && rejects(E::Busy, [&] { store.Delete("SlotA"); }));
    kt::save::Params params{"Alpha", "Subtitle", "Details", 11, 0}; store.SetParams(mount, params);
    check("metadata_update_round_trips_fields_and_preserves_quota", store.Describe(mount).params.title == "Alpha" && store.Describe(mount).params.user == 11 && store.Describe(mount).blocks == 96);
    auto invalid_params = params; invalid_params.title = "\xc0\x80";
    check("invalid_metadata_encoding_preserves_committed_description", rejects(E::Invalid, [&] { store.SetParams(mount, invalid_params); }) && store.Describe(mount).params.title == "Alpha");
    const std::array<std::byte, 8> icon{std::byte{137}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'}, std::byte{13}, std::byte{10}, std::byte{26}, std::byte{10}};
    store.SaveIcon(mount, icon);
    check("icon_is_staged_with_the_slot_metadata", fs::file_size(root / ".kt-transactions" / "slota" / "work" / "sce_sys" / "icon0.png") == icon.size());
    file->Flush(); file.reset(); store.Unmount(mount);
    check("stale_mount_cannot_open_new_files", rejects(E::NotMounted, [&] { (void)store.Open(mount, "data.bin", {.read = true}); }));
    {
        kt::save::Store reopened(root); auto readonly = reopened.MountSlot("SlotA", false, false, false, 0, created);
        auto persisted = reopened.Open(readonly, "data.bin", {.read = true}); readback.fill(std::byte{91});
        check("independent_store_reopens_flushed_payload", persisted->ReadAt(0, readback) == payload.size() && std::equal(payload.begin(), payload.end(), readback.begin()) && readback[payload.size()] == std::byte{91});
        check("readonly_descriptor_and_mount_never_gain_write_access", rejects(E::BadFile, [&] { (void)persisted->WriteAt(0, payload, end); }) &&
            rejects(E::ReadOnly, [&] { (void)reopened.Open(readonly, "new.bin", {.write = true, .create = true}); }) && rejects(E::ReadOnly, [&] { reopened.SetParams(readonly, params); }));
        persisted.reset(); reopened.Unmount(readonly);
    }
    mount = store.MountSlot("SlotA", true, false, false, 0, created);
    file = store.Open(mount, "data.bin", {.read = true, .write = true, .append = true});
    check("append_uses_live_eof_and_reports_actual_end", file->WriteAt(0, payload, end) == payload.size() && end == payload.size() * 2 && file->Info().size == end);
    check("directory_flags_cannot_truncate_regular_data", rejects(E::Invalid, [&] { (void)store.Open(mount, "data.bin", {.read = true, .write = true, .truncate = true, .directory = true}); }) && file->Info().size == payload.size() * 2);
    bool escaped = false;
    for (const auto path : {"../escape", "NUL", "file:stream", "file.", "sub//file", "sub\\file", "/absolute", "sce_sys/param.sfo"}) {
        escaped |= !rejects(E::Invalid, [&] { (void)store.Open(mount, path, {.write = true, .create = true}); });
    }
    check("file_namespace_rejects_escape_aliases_and_direct_metadata_access", !escaped);
    check("exclusive_create_preserves_existing_file", rejects(E::Exists, [&] { (void)store.Open(mount, "data.bin", {.write = true, .create = true, .exclusive = true}); }) && file->Info().size == payload.size() * 2);
    auto only_write = store.Open(mount, "data.bin", {.write = true});
    check("write_only_descriptor_cannot_read", rejects(E::BadFile, [&] { (void)only_write->ReadAt(0, readback); })); only_write.reset();
    auto folder = store.Open(mount, "", {.read = true, .directory = true});
    check("directory_stat_is_real_and_data_read_is_rejected", folder->Info().directory && rejects(E::Directory, [&] { (void)folder->ReadAt(0, readback); })); folder.reset();
    const auto alias = directory / "authored-external-alias.bin";
    check("hardlink_test_uses_only_authored_temporary_payload", CreateHardLinkW(alias.c_str(), (root / ".kt-transactions" / "slota" / "work" / "data.bin").c_str(), nullptr) != FALSE);
    check("existing_writable_descriptor_rejects_new_hardlink", rejects(E::Permission, [&] { (void)file->WriteAt(0, payload, end); }) &&
        fs::file_size(alias) == payload.size() * 2);
    check("hardlink_truncate_and_create_preserve_external_alias", rejects(E::Permission, [&] {
        (void)store.Open(mount, "data.bin", {.write = true, .create = true, .truncate = true});
    }) && fs::file_size(alias) == payload.size() * 2 && file->Info().size == payload.size() * 2);
    auto linked_read = store.Open(mount, "data.bin", {.read = true});
    std::array<std::byte, payload.size() * 2 + 1> linked_bytes; linked_bytes.fill(std::byte{91});
    check("hardlink_protection_preserves_original_payload_and_read_access", linked_read->ReadAt(0, linked_bytes) == payload.size() * 2 &&
        std::equal(payload.begin(), payload.end(), linked_bytes.begin()) && std::equal(payload.begin(), payload.end(), linked_bytes.begin() + payload.size()) &&
        linked_bytes.back() == std::byte{91});
    linked_read.reset();
    // Remove only the alias created by this self-test. The staged payload must
    // again be singly linked before it becomes the public save generation.
    if (!fs::remove(alias)) { throw std::runtime_error("Cannot retire authored hardlink test alias"); }
    file->Flush(); file.reset(); store.Unmount(mount);
    auto second = store.MountSlot("SlotB", true, true, true, 128, created); params.title = "Beta"; params.user = 3; store.SetParams(second, params); store.Unmount(second);
    auto names = store.Search("sLOT%", kt::save::SortKey::Name, true);
    check("sdk_percent_wildcard_and_case_insensitive_filter", names.size() == 2 && names[0].name == "SlotB" && names[1].name == "SlotA");
    check("sdk_underscore_and_trailing_percent_match", store.Search("Slot_", kt::save::SortKey::Name, false).size() == 2 && store.Search("SlotA%", kt::save::SortKey::Name, false).size() == 1);
    names = store.Search("%", kt::save::SortKey::User, false);
    check("sort_by_user_parameter_and_real_quota", names.size() == 2 && names[0].name == "SlotB" && names[0].blocks == 128 && names[1].blocks == 96 && names[0].free_blocks < names[0].blocks);
    DirectoryName pattern{"Slot%"}; SearchCondition condition{0x1000, 0, nullptr, &pattern, 0, 1, {}};
    std::array<DirectoryName, 2> name_output; std::memset(name_output.data(), 0xa5, sizeof(name_output));
    std::array<SaveParam, 2> param_output; std::memset(param_output.data(), 0xa5, sizeof(param_output));
    std::array<SearchInfo, 2> info_output; std::memset(info_output.data(), 0xa5, sizeof(info_output));
    SearchResult result; std::memset(&result, 0xa5, sizeof(result)); result.names = name_output.data(); result.capacity = 1; result.params = param_output.data(); result.infos = info_output.data();
    auto expected_result = result; expected_result.hits = 2; expected_result.set = 1;
    check("search_abi_reports_total_and_bounded_set_without_overwriting_result_fields", SearchHost(&condition, &result) == 0 && std::memcmp(&result, &expected_result, sizeof(result)) == 0 && std::string_view(name_output[0].value) == "SlotB");
    check("search_abi_fills_params_and_infos_and_preserves_array_tails", std::string_view(param_output[0].title) == "Beta" && param_output[0].user == 3 && info_output[0].blocks == 128 &&
        std::all_of(std::begin(name_output[1].value), std::end(name_output[1].value), [](char c) { return static_cast<unsigned char>(c) == 0xa5; }) &&
        std::all_of(std::begin(info_output[0].reserved), std::end(info_output[0].reserved), [](auto c) { return c == 0xa5; }) && static_cast<unsigned char>(param_output[1].title[0]) == 0xa5);
    result.hits = 73; result.set = 91; condition.key = 4; const auto before_error = result;
    check("invalid_search_key_preserves_outputs", SearchHost(&condition, &result) == kParameter && std::memcmp(&before_error, &result, sizeof(result)) == 0); condition.key = 0;
    SearchResult count_only{};
    check("zero_capacity_search_reports_hits_without_array_writes", SearchHost(&condition, &count_only) == 0 && count_only.hits == 2 && count_only.set == 0);
    condition.user = 0x1001;
    check("search_rejects_foreign_user_without_hiding_data", SearchHost(&condition, &result) == kInvalidUser && result.hits == 73); condition.user = 0x1000;
    const DirectoryName second_name{"SlotB"}; MountRequest request{0x1000, 0, &second_name, 0, 1, {}, 0}; MountResult mounted{};
    check("native_read_mount_returns_real_mountpoint_and_existing_status", Mount2(&request, &mounted) == 0 && std::string_view(mounted.point) == "/savedata0" && mounted.status == 0);
    MountPoint point{}; std::memcpy(point.value, mounted.point, sizeof(point.value)); SaveParam parameter{}; std::memcpy(parameter.title, "Replacement", 12);
    Icon icon_request{icon.data(), icon.size(), icon.size(), {}};
    check("native_metadata_writes_reject_readonly_mount", SetParamHost(&point, 0, &parameter, sizeof(parameter)) == static_cast<int>(0x809f000du) && SaveIconHost(&point, &icon_request) == static_cast<int>(0x809f000du));
    check("native_unmount_retires_identity_and_allows_termination", Unmount(&point) == 0 && Unmount(&point) == kNotMounted && !store.HasMounts());
    store.Delete("SlotA");
    check("deletion_is_recoverable_and_removed_slot_is_not_enumerated", !fs::exists(root / "SlotA") && fs::is_directory(root / ".trash") && store.Search("Slot%", kt::save::SortKey::Name, false).size() == 1);
    bool recovered = false; for (const auto& item : fs::directory_iterator(root / ".trash")) { recovered |= fs::is_regular_file(item.path() / "data.bin") && fs::file_size(item.path() / "data.bin") == payload.size() * 2; }
    check("recovery_directory_retains_payload_and_metadata", recovered);
    check("store_terminates_after_all_mounts_and_files_close", Terminate() == 0 && DirectorySearch(&condition, &result) == kNotInitialized);
    std::printf("{\"event\":\"save_store_self_test\",\"cases\":%u,\"passed\":%s}\n", cases, passed ? "true" : "false"); return passed;
}

bool SaveCrashFixture(const std::filesystem::path& directory, std::string_view phase) {
    namespace fs = std::filesystem;
    const bool fresh_slot = phase.starts_with("new_");
    const bool recover_only = phase.starts_with("recover_");
    const auto target_phase = fresh_slot ? phase.substr(4) : recover_only ? phase.substr(8) : phase;
    if (phase != "seed" && phase != "inspect" && phase != "inspect_fresh" && phase != "partial" && phase != "closed" &&
        phase != "ready" && phase != "previous_moved" && phase != "published" && phase != "flush_error" && phase != "prepare_flush_error" &&
        phase != "prepare_retry" && phase != "publication_retry" && phase != "cleanup_pending" && phase != "complete" &&
        !(fresh_slot && (target_phase == "partial" || target_phase == "closed" || target_phase == "ready" || target_phase == "published")) &&
        !(recover_only && (target_phase == "previous_moved" || target_phase == "published"))) {
        throw std::runtime_error("Unknown authored save crash-fixture phase");
    }
    ConfigureNativeStorage(directory);
    constexpr char marker[] = "Ketsui authored save interruption fixture v1";
    const auto marker_path = directory / ".kt-authored-crash-fixture";
    if (phase == "seed") {
        if (!fs::is_directory(directory) || !fs::is_empty(directory)) { throw std::runtime_error("Save crash fixture requires a fresh empty directory"); }
        const auto handle = CreateFileW(marker_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written{};
        if (handle == INVALID_HANDLE_VALUE) { throw std::runtime_error("Cannot mark authored save fixture"); }
        const bool written_ok = WriteFile(handle, marker, sizeof(marker), &written, nullptr) && written == sizeof(marker) && FlushFileBuffers(handle);
        CloseHandle(handle);
        if (!written_ok) { throw std::runtime_error("Cannot persist authored save fixture marker"); }
    } else {
        const auto handle = CreateFileW(marker_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        std::array<char, sizeof(marker) + 1> actual{}; DWORD received{};
        if (handle == INVALID_HANDLE_VALUE) { throw std::runtime_error("Save crash fixture marker is absent"); }
        BY_HANDLE_FILE_INFORMATION marker_info{};
        const bool marked = GetFileInformationByHandle(handle, &marker_info) && marker_info.nNumberOfLinks == 1 &&
            !(marker_info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
            ReadFile(handle, actual.data(), static_cast<DWORD>(actual.size()), &received, nullptr) &&
            received == sizeof(marker) && std::memcmp(actual.data(), marker, sizeof(marker)) == 0;
        CloseHandle(handle);
        if (!marked) { throw std::runtime_error("Save crash fixture marker does not match"); }
    }
    const auto root = directory / "profiles" / "1000" / "CUSA09062" / "savedata";
    kt::save::Store::CreateRoot(root);
    struct FixtureHandle { HANDLE value = INVALID_HANDLE_VALUE; ~FixtureHandle() { Reset(); } void Reset() { if (value != INVALID_HANDLE_VALUE) { CloseHandle(value); value = INVALID_HANDLE_VALUE; } } } held;
    const auto checkpoint = [&](std::string_view point) {
        if (phase == "flush_error" && point == "file_flush") { throw kt::save::Failure(kt::save::Error::Io, "Authored file flush failure"); }
        if (phase == "prepare_flush_error" && point == "commit_file_flush") { throw kt::save::Failure(kt::save::Error::Io, "Authored prepared-tree flush failure"); }
        if (phase == "cleanup_pending" && point == "published") {
            held.value = CreateFileW((root / ".kt-transactions" / "fixture" / "previous" / "data.bin").c_str(),
                GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (held.value == INVALID_HANDLE_VALUE) { throw std::runtime_error("Cannot hold authored retired payload"); }
            return;
        }
        if (point != target_phase) { return; }
        std::printf("{\"event\":\"save_crash_fixture_checkpoint\",\"point\":\"%.*s\"}\n", static_cast<int>(phase.size()), phase.data());
        std::fflush(stdout);
        for (;;) { Sleep(INFINITE); } // only this explicit, authored fixture can park here
    };
    kt::save::Store store(root, checkpoint); bool created{};
    if (phase == "inspect" || phase == "inspect_fresh" || recover_only) {
        auto mount = store.MountSlot(phase == "inspect_fresh" ? "Fresh" : "Fixture", false, false, false, 0, created);
        auto file = store.Open(mount, "data.bin", {.read = true});
        std::vector<std::byte> data(static_cast<std::size_t>(file->Info().size));
        const auto read = file->ReadAt(0, data); file.reset();
        const auto description = store.Describe(mount);
        store.Unmount(mount);
        const bool old = data.size() == 65536 && std::all_of(data.begin(), data.end(), [](auto value) { return value == std::byte{'A'}; });
        const bool next = data.size() == 65536 && std::all_of(data.begin(), data.end(), [](auto value) { return value == std::byte{'B'}; });
        std::printf("{\"event\":\"save_crash_fixture_inspected\",\"bytes\":%zu,\"read\":%zu,\"old_payload\":%s,\"new_payload\":%s,\"old_title\":%s,\"new_title\":%s}\n",
            data.size(), read, old ? "true" : "false", next ? "true" : "false",
            description.params.title == "Original" ? "true" : "false", description.params.title == "Updated" ? "true" : "false");
        return true;
    }
    const bool seed = phase == "seed";
    auto mount = store.MountSlot(fresh_slot ? "Fresh" : "Fixture", true, seed || fresh_slot, seed || fresh_slot, 96, created);
    store.SetParams(mount, {seed ? "Original" : "Updated", "Authored", "No game data", seed ? 1u : 2u, 0});
    const std::array<std::byte, 8> icon{std::byte{137}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'}, std::byte{13}, std::byte{10}, std::byte{26}, seed ? std::byte{10} : std::byte{11}};
    store.SaveIcon(mount, icon);
    auto file = store.Open(mount, "data.bin", {.read = true, .write = true, .create = true, .truncate = true});
    std::vector<std::byte> data(phase == "complete" ? 2096640 : 65536, seed ? std::byte{'A'} : std::byte{'B'}); std::uint64_t end{};
    (void)file->WriteAt(0, std::span(data).first(4096), end);
    if (phase == "flush_error") {
        bool flush_failed{}, commit_blocked{};
        try { file->Flush(); } catch (const kt::save::Failure& error) { flush_failed = error.error == kt::save::Error::Io; }
        file.reset();
        try { store.Unmount(mount); } catch (const kt::save::Failure& error) { commit_blocked = error.error == kt::save::Error::Io; }
        std::printf("{\"event\":\"save_crash_fixture_flush_error\",\"flush_failed\":%s,\"commit_blocked\":%s}\n", flush_failed ? "true" : "false", commit_blocked ? "true" : "false");
        return flush_failed && commit_blocked;
    }
    file->Flush();
    checkpoint("partial");
    (void)file->WriteAt(4096, std::span(data).subspan(4096), end); file->Flush(); file.reset();
    file = store.Open(mount, "second.bin", {.write = true, .create = true, .truncate = true});
    std::vector<std::byte> second(32768, seed ? std::byte{'C'} : std::byte{'D'});
    (void)file->WriteAt(0, second, end); file->Flush(); file.reset();
    checkpoint("closed");
    if (phase == "prepare_flush_error") {
        bool blocked{};
        try { store.Unmount(mount); } catch (const kt::save::Failure& error) { blocked = error.error == kt::save::Error::Io; }
        std::printf("{\"event\":\"save_crash_fixture_prepare_flush_error\",\"commit_blocked\":%s}\n", blocked ? "true" : "false");
        return blocked;
    }
    if (phase == "prepare_retry" || phase == "publication_retry") {
        const auto blocked = phase == "prepare_retry" ? root / ".kt-transactions" / "fixture" / "state.bin" : root / "Fixture";
        held.value = CreateFileW(blocked.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (held.value == INVALID_HANDLE_VALUE) { throw std::runtime_error("Cannot hold authored publication checkpoint"); }
        bool retryable{}, mutation_blocked{}; unsigned failure_kind{}; DWORD failure_win32{};
        try { store.Unmount(mount); } catch (const kt::save::Failure& error) {
            failure_kind = static_cast<unsigned>(error.error); failure_win32 = error.native_error;
            // Replacing an occupied record returns ACCESS_DENIED here, while
            // renaming the occupied directory returns SHARING_VIOLATION. Keep
            // both real errors; retry only after this fixture releases its hold.
            retryable = error.error == kt::save::Error::Busy || error.error == kt::save::Error::Permission;
        }
        try { store.SetParams(mount, {"Must not mutate prepared work", "", "", 7, 0}); }
        catch (const kt::save::Failure& error) { mutation_blocked = error.error == kt::save::Error::Busy; }
        held.Reset();
        if (!retryable || !mutation_blocked) { throw std::runtime_error("Authored commit failure did not preserve retry state"); }
        store.Unmount(mount);
        std::printf("{\"event\":\"save_crash_fixture_retry\",\"initially_blocked\":true,\"mutation_blocked\":true,\"retry_committed\":true,\"failure_kind\":%u,\"failure_win32\":%lu}\n", failure_kind, failure_win32);
        return true;
    }
    store.Unmount(mount);
    if (phase == "cleanup_pending") {
        const bool pending = fs::is_directory(root / ".kt-transactions" / "fixture" / "previous");
        held.Reset();
        std::printf("{\"event\":\"save_crash_fixture_cleanup_pending\",\"committed\":true,\"cleanup_pending\":%s}\n", pending ? "true" : "false");
        return pending;
    }
    std::puts("{\"event\":\"save_crash_fixture_completed\"}");
    return true;
}
