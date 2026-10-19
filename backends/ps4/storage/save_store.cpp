#include "save_store.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

namespace kt::save {
namespace {
namespace fs = std::filesystem;
constexpr std::uint64_t kBlock = 32768;
std::uint64_t ClockUs() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
[[noreturn]] void Fail(Error code, const char* message) { throw Failure(code, message); }
[[noreturn]] void WinFail(const char* message) {
    const auto code = GetLastError();
    auto error = Error::Io;
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) { error = Error::NotFound; }
    else if (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS) { error = Error::Exists; }
    else if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) { error = Error::Busy; }
    else if (code == ERROR_DISK_FULL || code == ERROR_HANDLE_DISK_FULL) { error = Error::NoSpace; }
    else if (code == ERROR_ACCESS_DENIED) { error = Error::Permission; }
    // Preserve the code at the failing call: RAII CloseHandle/DeleteFile during
    // unwinding may reset the thread's GetLastError before the caller sees it.
    throw Failure(error, message, code);
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) { other.value = INVALID_HANDLE_VALUE; }
    Handle& operator=(Handle&& other) noexcept { if (this != &other) { Reset(); value = other.value; other.value = INVALID_HANDLE_VALUE; } return *this; }
    void Reset() noexcept { if (value != INVALID_HANDLE_VALUE) { CloseHandle(value); value = INVALID_HANDLE_VALUE; } }
    ~Handle() { Reset(); }
};
Handle DirectoryGuard(const fs::path& directory) {
    Handle handle(CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle.value == INVALID_HANDLE_VALUE) { WinFail("Cannot guard save directory"); }
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!GetFileInformationByHandleEx(handle.value, FileAttributeTagInfo, &info, sizeof(info))) { WinFail("Cannot inspect save directory"); }
    if (!(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { Fail(Error::Invalid, "Save directory is not a plain directory"); }
    return handle;
}
std::vector<Handle> GuardParents(const fs::path& path) {
    std::vector<fs::path> paths;
    for (auto current = path; !current.empty();) {
        paths.push_back(current); const auto parent = current.parent_path(); if (parent == current) { break; } current = parent;
    }
    std::reverse(paths.begin(), paths.end()); std::vector<Handle> guards; guards.reserve(paths.size());
    for (const auto& directory : paths) { guards.push_back(DirectoryGuard(directory)); }
    return guards;
}
char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : c; }
bool Reserved(std::string_view name) {
    std::string stem(name.substr(0, name.find('.'))); for (auto& c : stem) { c = Lower(c); }
    return stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || stem == "conin$" || stem == "conout$" ||
        (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9');
}
void SlotName(std::string_view name) {
    if (name.empty() || name.size() >= 32 || Reserved(name)) { Fail(Error::Invalid, "Invalid save slot name"); }
    for (const auto c : name) { if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) { Fail(Error::Invalid, "Invalid save slot character"); } }
}
bool Match(std::string_view name, std::string_view pattern) {
    // SDK patterns use SQL-style % and _, not filesystem * and ?. Bounded DP
    // handles trailing % and avoids exponential recursive wildcard matching.
    if (name.size() >= 32 || pattern.size() >= 32) { Fail(Error::Invalid, "Oversized save search pattern"); }
    if (pattern.empty()) { return true; }
    std::array<bool, 32> previous{}, next{}; previous[0] = true;
    for (const auto p : pattern) {
        next.fill(false); next[0] = p == '%' && previous[0];
        for (std::size_t i = 1; i <= name.size(); ++i) {
            next[i] = p == '%' ? (previous[i] || next[i - 1]) : (previous[i - 1] && (p == '_' || Lower(p) == Lower(name[i - 1])));
        }
        previous = next;
    }
    return previous[name.size()];
}
fs::path Relative(std::string_view input) {
    if (input.empty()) { return {}; }
    if (input.size() > 255) { Fail(Error::Invalid, "Save file path is too long"); }
    std::wstring wide(input.size(), L'\0');
    const auto n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), wide.data(), static_cast<int>(wide.size()));
    if (!n) { Fail(Error::Invalid, "Invalid UTF-8 save path"); } wide.resize(n);
    std::size_t start{}; bool first = true;
    while (start < input.size()) {
        const auto slash = input.find('/', start), end = slash == std::string_view::npos ? input.size() : slash;
        const auto part = input.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ' || Reserved(part)) { Fail(Error::Invalid, "Invalid save path component"); }
        for (const unsigned char c : part) { if (c < 32 || std::string_view("\\:*?\"<>|").find(static_cast<char>(c)) != std::string_view::npos) { Fail(Error::Invalid, "Invalid save path character"); } }
        std::string lower(part); for (auto& c : lower) { c = Lower(c); }
        if (first && lower == "sce_sys") { Fail(Error::Invalid, "Save metadata is managed through its API"); }
        first = false; if (slash == std::string_view::npos) { break; } start = slash + 1;
        if (start == input.size()) { Fail(Error::Invalid, "Trailing save path separator"); }
    }
    fs::path result(wide);
    if (result.has_root_name() || result.has_root_directory()) { Fail(Error::Invalid, "Host paths are not save paths"); }
    return result;
}
kt::content::NodeInfo Node(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{}; FILE_STANDARD_INFO standard{}; FILE_BASIC_INFO basic{};
    if (!GetFileInformationByHandle(handle, &info) || !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
        !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic))) { WinFail("Cannot inspect save file"); }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { Fail(Error::Invalid, "Reparse points are not save files"); }
    return {standard.Directory != FALSE, info.dwVolumeSerialNumber, info.nFileIndexLow, info.nNumberOfLinks,
        standard.Directory ? 0 : static_cast<std::uint64_t>(standard.EndOfFile.QuadPart), static_cast<std::uint64_t>(standard.AllocationSize.QuadPart),
        static_cast<std::uint64_t>(basic.LastAccessTime.QuadPart), static_cast<std::uint64_t>(basic.LastWriteTime.QuadPart),
        static_cast<std::uint64_t>(basic.ChangeTime.QuadPart), static_cast<std::uint64_t>(basic.CreationTime.QuadPart)};
}
Handle ReadHandle(const fs::path& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) { WinFail("Cannot open save file"); }
    (void)Node(file.value); return file;
}
std::uint64_t Used(const fs::path& root) {
    std::uint64_t bytes{}; unsigned count{};
    for (const auto& item : fs::recursive_directory_iterator(root)) {
        if (++count > 16384) { Fail(Error::Io, "Too many save entries"); }
        const auto attr = GetFileAttributesW(item.path().c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) { WinFail("Cannot inspect save usage"); }
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT) { Fail(Error::Invalid, "Linked save entries are not supported"); }
        if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            const auto size = item.file_size(); if (size > (1ull << 30) || bytes > (1ull << 30) - size) { Fail(Error::NoSpace, "Save slot exceeds supported quota"); } bytes += size;
        }
    }
    return bytes;
}
kt::content::Sfo Metadata(const fs::path& slot) {
    try {
        auto guards = GuardParents(slot / "sce_sys"); auto file = ReadHandle(slot / "sce_sys" / "param.sfo");
        const auto info = Node(file.value); if (info.directory || info.size > 1024 * 1024) { Fail(Error::Corrupt, "Invalid save metadata size"); }
        std::vector<std::byte> bytes(static_cast<std::size_t>(info.size)); DWORD read{};
        if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()) { WinFail("Cannot read save metadata"); }
        return kt::content::Sfo::Parse(bytes);
    } catch (const Failure&) { throw; } catch (const std::exception&) { Fail(Error::Corrupt, "Invalid save metadata"); }
}
std::uint64_t Blocks(const kt::content::Sfo& metadata) {
    const auto value = metadata.Binary("SAVEDATA_BLOCKS");
    if (!value || value->size() != 8) { Fail(Error::Corrupt, "Save quota metadata is missing"); }
    std::uint64_t result{}; for (unsigned i = 0; i < 8; ++i) { result |= std::uint64_t{std::to_integer<unsigned>((*value)[i])} << (i * 8); }
    if (result < 96 || result > 32768) { Fail(Error::Corrupt, "Invalid save quota"); } return result;
}
std::wstring Unique(const wchar_t* prefix) {
    static std::atomic<unsigned> serial{};
    return std::wstring(prefix) + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++serial);
}
void AtomicWrite(const fs::path& destination, std::span<const std::byte> bytes) {
    auto guards = GuardParents(destination.parent_path()); const auto temporary = destination.parent_path() / Unique(L".kt-write-");
    Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) { WinFail("Cannot create save transaction file"); }
    try {
        std::size_t total{};
        while (total < bytes.size()) {
            const auto amount = static_cast<DWORD>((std::min)(bytes.size() - total, std::size_t{1} << 20)); DWORD written{};
            if (!WriteFile(file.value, bytes.data() + total, amount, &written, nullptr)) { WinFail("Cannot write save transaction"); }
            if (written != amount) { Fail(Error::Io, "Short save transaction write"); } total += written;
        }
        if (!FlushFileBuffers(file.value)) { WinFail("Cannot flush save transaction"); } file.Reset();
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { WinFail("Cannot commit save transaction"); }
    } catch (...) { file.Reset(); DeleteFileW(temporary.c_str()); throw; }
}
Entry DescribePath(const fs::path& path, const std::string& name) {
    const auto metadata = Metadata(path); Entry result; result.name = name; result.blocks = Blocks(metadata);
    result.params.title = metadata.Text("MAINTITLE").value_or(""); result.params.subtitle = metadata.Text("SUBTITLE").value_or("");
    result.params.detail = metadata.Text("DETAIL").value_or(""); result.params.user = static_cast<std::uint32_t>(metadata.Integer("SAVEDATA_LIST_PARAM").value_or(0));
    if (result.params.title.size() >= 128 || result.params.subtitle.size() >= 128 || result.params.detail.size() >= 1024) { Fail(Error::Corrupt, "Invalid save description bounds"); }
    auto file = ReadHandle(path / "sce_sys" / "param.sfo"); const auto ticks = Node(file.value).modification_time;
    constexpr std::uint64_t epoch = 116444736000000000ull;
    result.params.modified = ticks >= epoch ? static_cast<std::int64_t>((ticks - epoch) / 10000000) : 0;
    const auto used = Used(path); const auto occupied = used / kBlock + (used % kBlock != 0);
    result.free_blocks = occupied >= result.blocks ? 0 : result.blocks - occupied; return result;
}

struct DirectoryId {
    std::uint32_t volume{}, high{}, low{};
    bool operator==(const DirectoryId&) const = default;
};
DirectoryId Identity(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) { WinFail("Cannot identify save directory"); }
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { Fail(Error::Invalid, "Save transaction directory is not plain"); }
    if (!info.nFileIndexHigh && !info.nFileIndexLow) { Fail(Error::Invalid, "Save filesystem does not provide directory identities"); }
    return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
}
DirectoryId Identity(const fs::path& path) { auto guard = DirectoryGuard(path); return Identity(guard.value); }
bool Exists(const fs::path& path) {
    const auto attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) { return true; }
    const auto error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) { return false; }
    WinFail("Cannot inspect save transaction path");
}
std::string SlotKey(std::string_view name) { SlotName(name); std::string key(name); for (auto& c : key) { c = Lower(c); } return key; }
Handle SlotLease(const fs::path& root, std::string_view name) {
    const auto path = root / (".kt-lock-" + SlotKey(name));
    Handle lease(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (lease.value == INVALID_HANDLE_VALUE) { WinFail("Save slot is owned by another mount"); }
    const auto info = Node(lease.value);
    if (info.directory || info.links != 1) { Fail(Error::Invalid, "Invalid save slot lock file"); }
    return lease;
}
void PlainDirectory(const fs::path& path) {
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) { WinFail("Cannot create save transaction directory"); }
    auto guard = DirectoryGuard(path);
}
struct CopyBudget { std::uint64_t bytes{}, quota{}; unsigned entries{}; };
void CloneTree(const fs::path& source, const fs::path& destination, CopyBudget& budget, unsigned depth = 0) {
    if (depth > 64) { Fail(Error::Invalid, "Save directory nesting exceeds the supported bound"); }
    auto guard = DirectoryGuard(source);
    std::vector<std::byte> buffer(65536);
    for (const auto& entry : fs::directory_iterator(source)) {
        if (++budget.entries > 16384) { Fail(Error::Io, "Too many save entries to stage"); }
        const auto attr = GetFileAttributesW(entry.path().c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) { WinFail("Cannot inspect save staging source"); }
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT) { Fail(Error::Invalid, "Linked save entries cannot be staged"); }
        const auto target = destination / entry.path().filename();
        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CreateDirectoryW(target.c_str(), nullptr)) { WinFail("Cannot stage save subdirectory"); }
            CloneTree(entry.path(), target, budget, depth + 1); continue;
        }
        auto input = ReadHandle(entry.path()); const auto info = Node(input.value);
        if (info.directory || info.links != 1) { Fail(Error::Permission, "Hard-linked save files cannot be staged for writes"); }
        if (info.size > budget.quota || budget.bytes > budget.quota - info.size) { Fail(Error::NoSpace, "Existing save exceeds its quota"); }
        budget.bytes += info.size;
        Handle output(CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (output.value == INVALID_HANDLE_VALUE) { WinFail("Cannot create save staging file"); }
        std::uint64_t copied{};
        while (copied < info.size) {
            const auto amount = static_cast<DWORD>((std::min)(info.size - copied, static_cast<std::uint64_t>(buffer.size()))); DWORD received{}, written{};
            if (!ReadFile(input.value, buffer.data(), amount, &received, nullptr)) { WinFail("Cannot read save staging source"); }
            if (received != amount) { Fail(Error::Io, "Short save staging source read"); }
            if (!WriteFile(output.value, buffer.data(), amount, &written, nullptr)) { WinFail("Cannot write save staging copy"); }
            if (written != amount) { Fail(Error::Io, "Short save staging copy write"); }
            copied += amount;
        }
        // Uncommitted copies may stay buffered. Every surviving file is
        // flushed and checked together before the prepared record is issued.
        FILETIME creation{}, access{}, write{};
        if (!GetFileTime(input.value, &creation, &access, &write) || !SetFileTime(output.value, &creation, &access, &write)) { WinFail("Cannot preserve save staging timestamps"); }
    }
}
void ValidateStagedTree(const fs::path& path, CopyBudget& budget,
                        const std::function<void(std::string_view)>& checkpoint, unsigned depth = 0) {
    if (depth > 64) { Fail(Error::Invalid, "Save commit nesting exceeds the supported bound"); }
    auto guard = DirectoryGuard(path);
    for (const auto& entry : fs::directory_iterator(path)) {
        if (++budget.entries > 16384) { Fail(Error::Io, "Too many save commit entries"); }
        const auto attr = GetFileAttributesW(entry.path().c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) { WinFail("Cannot inspect save commit entry"); }
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT) { Fail(Error::Invalid, "Linked save commit entries are not supported"); }
        if (attr & FILE_ATTRIBUTE_DIRECTORY) { ValidateStagedTree(entry.path(), budget, checkpoint, depth + 1); continue; }
        Handle file(CreateFileW(entry.path().c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (file.value == INVALID_HANDLE_VALUE) { WinFail("Cannot open staged save for commit flush"); }
        const auto info = Node(file.value);
        if (info.directory || info.links != 1) { Fail(Error::Permission, "Hard-linked save data cannot be committed"); }
        if (info.size > budget.quota || budget.bytes > budget.quota - info.size) { Fail(Error::NoSpace, "Save commit exceeds quota"); }
        budget.bytes += info.size;
        if (checkpoint) { checkpoint("commit_file_flush"); }
        if (!FlushFileBuffers(file.value)) { WinFail("Cannot flush complete staged save"); }
    }
}
void TouchMetadata(const fs::path& path) {
    auto guards = GuardParents(path.parent_path());
    Handle file(CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) { WinFail("Cannot open save metadata timestamp"); }
    const auto info = Node(file.value);
    if (info.directory || info.links != 1) { Fail(Error::Permission, "Linked save metadata timestamp is not changed"); }
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    if (!SetFileTime(file.value, nullptr, nullptr, &now)) { WinFail("Cannot update save metadata timestamp"); }
    // ValidateStagedTree flushes the same stream, including this metadata
    // update. Re-serializing unchanged PSF bytes would add a redundant file
    // replacement and disk barrier on every save.
}
// Only transaction-owned trees reach this function, after their directory
// identity has been checked. No recursive operation follows a reparse point.
void RemovePlainTree(const fs::path& path, unsigned depth = 0) {
    if (depth > 64) { Fail(Error::Invalid, "Save cleanup nesting exceeds the supported bound"); }
    { auto guard = DirectoryGuard(path);
      for (const auto& entry : fs::directory_iterator(path)) {
        const auto attr = GetFileAttributesW(entry.path().c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) { WinFail("Cannot inspect save transaction cleanup entry"); }
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT) { Fail(Error::Invalid, "Linked save transaction cleanup is not followed"); }
        if (attr & FILE_ATTRIBUTE_DIRECTORY) { RemovePlainTree(entry.path(), depth + 1); }
        else if (!DeleteFileW(entry.path().c_str())) { WinFail("Cannot remove retired save transaction file"); }
      }
    }
    if (!RemoveDirectoryW(path.c_str())) { WinFail("Cannot remove retired save transaction directory"); }
}
struct TransactionRecord { std::string name; bool prepared{}, had_original{}; DirectoryId original{}, next{}, container{}; };
std::array<std::byte, 160> EncodeRecord(const TransactionRecord& record) {
    std::array<std::byte, 160> bytes{};
    std::memcpy(bytes.data(), "KTSAVTX1", 8);
    const auto put = [&](std::size_t at, std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) { bytes[at + i] = static_cast<std::byte>(value >> (8 * i)); } };
    put(8, 1); put(12, record.prepared ? 1 : 0); put(48, record.had_original ? 1 : 0);
    std::memcpy(bytes.data() + 16, record.name.data(), record.name.size());
    for (const auto& [offset, id] : {std::pair{56u, record.original}, std::pair{80u, record.next}, std::pair{104u, record.container}}) { put(offset, id.volume); put(offset + 4, id.high); put(offset + 8, id.low); }
    const auto digest = kt::content::ReadPatchHash(std::span(bytes).first(128));
    std::copy(digest.begin(), digest.end(), bytes.begin() + 128); return bytes;
}
TransactionRecord ReadRecord(const fs::path& path, std::string_view key) {
    auto file = ReadHandle(path); const auto info = Node(file.value);
    std::array<std::byte, 160> bytes{}; DWORD received{};
    if (info.directory || info.links != 1 || info.size != bytes.size() ||
        !ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &received, nullptr) || received != bytes.size()) { Fail(Error::Corrupt, "Invalid save transaction record size"); }
    const auto get = [&](std::size_t at) { std::uint32_t value{}; for (unsigned i = 0; i < 4; ++i) { value |= std::to_integer<std::uint32_t>(bytes[at + i]) << (8 * i); } return value; };
    const auto digest = kt::content::ReadPatchHash(std::span(bytes).first(128));
    if (std::memcmp(bytes.data(), "KTSAVTX1", 8) || get(8) != 1 || get(12) > 1 || get(48) > 1 ||
        !std::equal(digest.begin(), digest.end(), bytes.begin() + 128)) { Fail(Error::Corrupt, "Invalid save transaction record"); }
    const auto* text = reinterpret_cast<const char*>(bytes.data() + 16); const auto end = std::find(text, text + 32, '\0');
    if (end == text + 32) { Fail(Error::Corrupt, "Unterminated save transaction slot"); }
    TransactionRecord result; result.name.assign(text, end); result.prepared = get(12) != 0; result.had_original = get(48) != 0;
    std::string actual_key;
    try { actual_key = SlotKey(result.name); } catch (const Failure&) { Fail(Error::Corrupt, "Invalid save transaction slot namespace"); }
    if (actual_key != key) { Fail(Error::Corrupt, "Save transaction slot identity differs"); }
    result.original = {get(56), get(60), get(64)}; result.next = {get(80), get(84), get(88)};
    result.container = {get(104), get(108), get(112)};
    // Reject unknown/reserved fields, not just a self-consistent checksum.
    if (EncodeRecord(result) != bytes) { Fail(Error::Corrupt, "Noncanonical save transaction record"); }
    return result;
}
void MoveDirectory(const fs::path& source, const fs::path& target) {
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) { WinFail("Cannot publish save transaction directory"); }
}
void FinishPrepared(const fs::path& root, const fs::path& transaction, const TransactionRecord& record,
                    const std::function<void(std::string_view)>& checkpoint = {}) {
    auto transaction_guard = DirectoryGuard(transaction);
    if (Identity(transaction_guard.value) != record.container) { Fail(Error::Corrupt, "Save transaction container identity changed"); }
    const auto target = root / record.name, work = transaction / "work", previous = transaction / "previous";
    const bool target_exists = Exists(target), work_exists = Exists(work), previous_exists = Exists(previous);
    if (target_exists && Identity(target) == record.next) {
        if (work_exists || (previous_exists && (!record.had_original || Identity(previous) != record.original))) { Fail(Error::Corrupt, "Conflicting published save transaction identities"); }
        return;
    }
    if (!work_exists || Identity(work) != record.next) { Fail(Error::Corrupt, "Prepared save transaction work is missing or changed"); }
    if (record.had_original) {
        if (target_exists) {
            if (previous_exists || Identity(target) != record.original) { Fail(Error::Corrupt, "Original save transaction identity changed"); }
            MoveDirectory(target, previous);
            if (checkpoint) { checkpoint("previous_moved"); }
        } else if (!previous_exists || Identity(previous) != record.original) { Fail(Error::Corrupt, "Original save transaction backup is missing"); }
    } else if (target_exists || previous_exists) { Fail(Error::Corrupt, "New save transaction collides with existing data"); }
    MoveDirectory(work, target);
    if (checkpoint) { checkpoint("published"); }
}
void CleanupPrepared(const fs::path& root, const fs::path& transaction, const TransactionRecord& record) {
    auto transaction_guard = DirectoryGuard(transaction);
    if (Identity(transaction_guard.value) != record.container) { Fail(Error::Corrupt, "Save transaction cleanup container identity changed"); }
    if (Identity(root / record.name) != record.next || Exists(transaction / "work")) { Fail(Error::Corrupt, "Save transaction is not published"); }
    if (Exists(transaction / "previous")) {
        if (!record.had_original || Identity(transaction / "previous") != record.original) { Fail(Error::Corrupt, "Retired save directory identity changed"); }
        RemovePlainTree(transaction / "previous");
    }
    // Delete only our own known record and atomic-write scratch files; unknown
    // entries are preserved and reported instead of recursively removed.
    for (const auto& entry : fs::directory_iterator(transaction)) {
        const auto name = entry.path().filename().wstring();
        if ((name != L"state.bin" && !name.starts_with(L".kt-write-")) || fs::is_directory(entry.path())) { Fail(Error::Corrupt, "Unknown save transaction cleanup entry"); }
        const auto attr = GetFileAttributesW(entry.path().c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) { Fail(Error::Corrupt, "Invalid save transaction cleanup file"); }
        if (!DeleteFileW(entry.path().c_str())) { WinFail("Cannot remove completed save transaction record"); }
    }
    transaction_guard.Reset();
    if (!RemoveDirectoryW(transaction.c_str())) { WinFail("Cannot remove completed save transaction container"); }
}
void RecoverSlot(const fs::path& root, std::string_view key, const std::function<void(std::string_view)>& checkpoint = {}) {
    const auto base = root / ".kt-transactions", transaction = base / std::string(key);
    if (!Exists(transaction)) { return; }
    auto base_guard = DirectoryGuard(base);
    auto transaction_guard = DirectoryGuard(transaction);
    const auto state_path = transaction / "state.bin";
    if (Exists(state_path)) {
        const auto record = ReadRecord(state_path, key);
        if (Identity(transaction_guard.value) != record.container) { Fail(Error::Corrupt, "Save transaction recovery container identity changed"); }
        if (record.prepared) {
            FinishPrepared(root, transaction, record, checkpoint); transaction_guard.Reset(); CleanupPrepared(root, transaction, record);
            std::printf("{\"event\":\"native_save_transaction_recovered\",\"directory\":\"%s\",\"action\":\"finish_prepared_commit\"}\n", record.name.c_str()); return;
        }
        const auto target = root / record.name;
        if (record.had_original ? (!Exists(target) || Identity(target) != record.original) : Exists(target)) { Fail(Error::Corrupt, "Uncommitted save original identity changed"); }
    }
    if (Exists(transaction / "previous")) { Fail(Error::Corrupt, "Unprepared save has a retired original"); }
    const auto recovery = root / ".kt-recovery"; PlainDirectory(recovery); auto recovery_guard = DirectoryGuard(recovery);
    transaction_guard.Reset();
    MoveDirectory(transaction, recovery / (fs::path(key).wstring() + L"-" + Unique(L"interrupted-")));
    std::printf("{\"event\":\"native_save_transaction_recovered\",\"directory\":\"%.*s\",\"action\":\"retain_original_preserve_work\",\"recovery_directory\":\".kt-recovery\"}\n", static_cast<int>(key.size()), key.data());
}
void RecoverAll(const fs::path& root, const std::function<void(std::string_view)>& checkpoint) {
    const auto base = root / ".kt-transactions"; if (!Exists(base)) { return; }
    auto guard = DirectoryGuard(base); std::vector<std::string> keys;
    for (const auto& entry : fs::directory_iterator(base)) {
        const auto key = entry.path().filename().string();
        if (SlotKey(key) != key || keys.size() >= 4096) { Fail(Error::Corrupt, "Invalid save transaction directory name"); }
        keys.push_back(key);
    }
    for (const auto& key : keys) {
        Handle lease;
        try { lease = SlotLease(root, key); }
        catch (const Failure& error) { if (error.error == Error::Busy) { continue; } throw; } // live writer owns this transaction
        // A rename/cleanup sharing failure is not evidence of a live writer.
        // Report it instead of silently exposing a temporarily absent slot.
        RecoverSlot(root, key, checkpoint);
    }
}
struct Transaction {
    fs::path root, directory, work;
    Handle base_guard, directory_guard;
    TransactionRecord record;
    std::uint64_t copied_bytes{}, began_clock_us{}, stage_us{};
};
std::unique_ptr<Transaction> BeginTransaction(const fs::path& root, std::string_view name, bool exists, std::uint64_t blocks) {
    const auto began = ClockUs();
    auto transaction = std::make_unique<Transaction>(); transaction->root = root; transaction->began_clock_us = began;
    const auto base = root / ".kt-transactions"; PlainDirectory(base); transaction->base_guard = DirectoryGuard(base);
    transaction->directory = base / SlotKey(name); transaction->work = transaction->directory / "work";
    if (!CreateDirectoryW(transaction->directory.c_str(), nullptr)) { WinFail("Cannot create exclusive save transaction"); }
    transaction->directory_guard = DirectoryGuard(transaction->directory);
    auto& record = transaction->record; record.name = std::string(name); record.had_original = exists; record.container = Identity(transaction->directory_guard.value);
    try {
        if (exists) { record.original = Identity(root / record.name); }
        if (!CreateDirectoryW(transaction->work.c_str(), nullptr)) { WinFail("Cannot create save work directory"); }
        record.next = Identity(transaction->work); AtomicWrite(transaction->directory / "state.bin", EncodeRecord(record));
        if (exists) { CopyBudget budget{0, blocks * kBlock, 0}; CloneTree(root / record.name, transaction->work, budget); transaction->copied_bytes = budget.bytes; }
        else {
            if (!CreateDirectoryW((transaction->work / "sce_sys").c_str(), nullptr)) { WinFail("Cannot create save work metadata directory"); }
            kt::content::Sfo metadata; metadata.SetText("MAINTITLE", "Ketsui Deathtiny"); metadata.SetText("SUBTITLE", ""); metadata.SetText("DETAIL", "");
            metadata.SetText("TITLE_ID", "CUSA09062"); metadata.SetText("SAVEDATA_DIRECTORY", name); metadata.SetInteger("SAVEDATA_LIST_PARAM", 0);
            std::array<std::byte, 8> quota{}; for (unsigned i = 0; i < 8; ++i) { quota[i] = static_cast<std::byte>(blocks >> (i * 8)); }
            metadata.SetBinary("SAVEDATA_BLOCKS", quota); AtomicWrite(transaction->work / "sce_sys" / "param.sfo", metadata.Serialize());
        }
        transaction->stage_us = ClockUs() - began; return transaction;
    } catch (...) {
        // A failed mount was never published to the caller. Retire only this
        // newly owned staging tree so retrying a full disk cannot leak copies.
        transaction->directory_guard.Reset();
        try {
            if (Identity(transaction->directory) != record.container) { Fail(Error::Corrupt, "Failed staging container identity changed"); }
            RemovePlainTree(transaction->directory);
        } catch (const std::exception& error) { std::fprintf(stderr, "Failed save staging retained: %s\n", error.what()); }
        throw;
    }
}
} // namespace

struct Store::Impl {
    fs::path root; std::vector<Handle> guards; mutable std::mutex gate;
    std::map<std::string, std::weak_ptr<Mount>, std::less<>> mounts;
    std::function<void(std::string_view)> checkpoint;
};
struct Mount::Impl {
    std::shared_ptr<Store::Impl> store; Handle lease; std::string name; fs::path path;
    std::unique_ptr<Transaction> transaction; Handle guard;
    std::uint64_t blocks{}; unsigned open_files{}; bool readonly{}, active{}, dirty{}, failed{}, committing{};
};
struct File::Impl {
    std::shared_ptr<Mount> mount; Handle handle; std::vector<Handle> guards;
    OpenOptions options; bool dirty{};
};
Mount::Mount(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Mount::~Mount() = default;
bool Mount::ReadOnly() const { return impl_->readonly; }
const std::string& Mount::Name() const { return impl_->name; }
File::File(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
File::~File() {
    if (!impl_) { return; }
    auto& mount = *impl_->mount->impl_; std::lock_guard lock(mount.store->gate);
    if (impl_->dirty && !FlushFileBuffers(impl_->handle.value)) { mount.failed = true; std::fprintf(stderr, "Save close flush failed: %lu\n", GetLastError()); }
    impl_->handle.Reset(); impl_->guards.clear(); --mount.open_files;
}
bool File::ReadOnlyMount() const { return impl_->mount->ReadOnly(); }
kt::content::NodeInfo File::Info() const { std::lock_guard lock(impl_->mount->impl_->store->gate); return Node(impl_->handle.value); }
std::size_t File::ReadAt(std::uint64_t offset, std::span<std::byte> output) {
    std::lock_guard lock(impl_->mount->impl_->store->gate);
    if (!impl_->options.read) { Fail(Error::BadFile, "Save descriptor is not readable"); }
    const auto info = Node(impl_->handle.value); if (info.directory) { Fail(Error::Directory, "Cannot read directory data"); }
    if (offset >= info.size || output.empty()) { return 0; }
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(impl_->handle.value, position, nullptr, FILE_BEGIN)) { WinFail("Cannot seek save file"); }
    const auto wanted = (std::min)(info.size - offset, static_cast<std::uint64_t>(output.size())); std::size_t total{};
    while (total < wanted) {
        const auto amount = static_cast<DWORD>((std::min)(wanted - total, std::uint64_t{1} << 20)); DWORD received{};
        if (!ReadFile(impl_->handle.value, output.data() + total, amount, &received, nullptr)) { WinFail("Cannot read save file"); }
        total += received; if (received < amount) { break; }
    }
    return total;
}
std::size_t File::WriteAt(std::uint64_t offset, std::span<const std::byte> input, std::uint64_t& end) {
    auto& mount = *impl_->mount->impl_; std::lock_guard lock(mount.store->gate);
    if (!impl_->options.write) { Fail(Error::BadFile, "Save descriptor is not writable"); }
    const auto info = Node(impl_->handle.value); if (info.directory) { Fail(Error::Directory, "Cannot write directory data"); }
    if (info.links != 1) { Fail(Error::Permission, "Hard-linked save files are not writable"); }
    if (input.empty()) { end = offset; return 0; }
    if (impl_->options.append) { offset = info.size; }
    const auto quota = mount.blocks * kBlock;
    if (offset > quota || input.size() > quota - offset) { Fail(Error::NoSpace, "Save write exceeds quota"); }
    const auto next_size = (std::max)(info.size, offset + input.size()); const auto used = Used(mount.path);
    if (used > quota || next_size - info.size > quota - used) { Fail(Error::NoSpace, "Save slot is full"); }
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(impl_->handle.value, position, nullptr, FILE_BEGIN)) { WinFail("Cannot seek save output"); }
    std::size_t total{};
    while (total < input.size()) {
        const auto amount = static_cast<DWORD>((std::min)(input.size() - total, std::size_t{1} << 20)); DWORD written{};
        if (!WriteFile(impl_->handle.value, input.data() + total, amount, &written, nullptr)) { mount.failed = true; WinFail("Cannot write save file"); }
        if (!written) { mount.failed = true; Fail(Error::Io, "Save write made no progress"); }
        total += written; impl_->dirty = mount.dirty = true;
    }
    if (impl_->options.sync && !FlushFileBuffers(impl_->handle.value)) { mount.failed = true; WinFail("Cannot synchronize save output"); }
    end = offset + total; return total;
}
void File::Flush() {
    std::lock_guard lock(impl_->mount->impl_->store->gate);
    if (impl_->options.write || impl_->options.create) {
        try {
            if (impl_->mount->impl_->store->checkpoint) { impl_->mount->impl_->store->checkpoint("file_flush"); }
            if (!FlushFileBuffers(impl_->handle.value)) { WinFail("Cannot flush save file"); }
        } catch (...) { impl_->mount->impl_->failed = true; throw; }
    }
    impl_->dirty = false;
}
Store::Store(const fs::path& root, std::function<void(std::string_view)> checkpoint) : impl_(std::make_shared<Impl>()) {
    if (!root.is_absolute()) { Fail(Error::Invalid, "Save store root must be absolute"); }
    impl_->guards = GuardParents(root.lexically_normal()); impl_->root = fs::canonical(root);
    impl_->checkpoint = std::move(checkpoint); RecoverAll(impl_->root, impl_->checkpoint);
}
void Store::CreateRoot(const fs::path& root) {
    if (!root.is_absolute()) { Fail(Error::Invalid, "Save store root must be absolute"); }
    const auto normalized = root.lexically_normal();
    auto cursor = normalized.root_path();
    std::vector<Handle> guards;
    guards.push_back(DirectoryGuard(cursor));
    for (const auto& part : normalized.relative_path()) {
        cursor /= part;
        if (!CreateDirectoryW(cursor.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) { WinFail("Cannot create save store root"); }
        guards.push_back(DirectoryGuard(cursor));
    }
}
bool Store::HasMounts() const {
    std::lock_guard lock(impl_->gate);
    for (const auto& [name, weak] : impl_->mounts) { (void)name; if (const auto mount = weak.lock(); mount && mount->impl_->active) { return true; } }
    return false;
}
std::shared_ptr<Mount> Store::MountSlot(std::string_view name, bool writable, bool create, bool exclusive, std::uint64_t blocks, bool& created) {
    SlotName(name); std::lock_guard lock(impl_->gate); created = false;
    if ((create && !writable) || (create && (blocks < 96 || blocks > 32768))) { Fail(Error::Invalid, "Invalid save creation options"); }
    for (const auto& [other, weak] : impl_->mounts) { if (const auto mount = weak.lock(); mount && mount->impl_->active) {
        if (other == name || CompareStringOrdinal(fs::path(other).c_str(), -1, fs::path(name).c_str(), -1, TRUE) == CSTR_EQUAL) { Fail(Error::Busy, "Save slot is already mounted"); }
    } }
    const auto path = impl_->root / std::string(name);
    if (!Exists(path) && !create && !Exists(impl_->root / ".kt-transactions" / SlotKey(name))) { Fail(Error::NotFound, "Save slot is absent"); }
    auto lease = SlotLease(impl_->root, name); RecoverSlot(impl_->root, SlotKey(name), impl_->checkpoint);
    const bool exists = Exists(path);
    if (exists && exclusive) { Fail(Error::Exists, "Save slot already exists"); }
    if (!exists && !create) { Fail(Error::NotFound, "Save slot is absent"); }
    auto state = std::make_unique<Mount::Impl>(); state->store = impl_; state->name = std::string(name);
    state->lease = std::move(lease); state->path = path;
    if (exists) {
        auto guard = DirectoryGuard(path);
        try { state->blocks = Blocks(Metadata(path)); }
        catch (const Failure& error) { if (error.error == Error::NotFound) { Fail(Error::Corrupt, "Existing save has no committed metadata"); } throw; }
    } else { state->blocks = blocks; }
    if (writable) {
        state->transaction = BeginTransaction(impl_->root, name, exists, state->blocks);
        state->path = state->transaction->work;
    }
    state->guard = DirectoryGuard(state->path);
    state->readonly = !writable; state->active = true; state->dirty = !exists; created = !exists;
    auto mount = std::shared_ptr<Mount>(new Mount(std::move(state))); impl_->mounts.insert_or_assign(std::string(name), mount);
    if (mount->impl_->transaction) {
        const auto& transaction = *mount->impl_->transaction;
        std::printf("{\"event\":\"native_save_transaction_staged\",\"directory\":\"%s\",\"copy_bytes\":%llu,\"stage_us\":%llu,\"began_clock_us\":%llu,\"clock_us\":%llu,\"thread\":%lu}\n",
            transaction.record.name.c_str(), static_cast<unsigned long long>(transaction.copied_bytes),
            static_cast<unsigned long long>(transaction.stage_us), static_cast<unsigned long long>(transaction.began_clock_us),
            static_cast<unsigned long long>(transaction.began_clock_us + transaction.stage_us), GetCurrentThreadId());
    }
    return mount;
}
void Store::Unmount(const std::shared_ptr<Mount>& mount) {
    const auto began = ClockUs();
    std::lock_guard lock(impl_->gate);
    if (!mount || mount->impl_->store != impl_ || !mount->impl_->active) { Fail(Error::NotMounted, "Save mount is not active"); }
    auto& state = *mount->impl_; if (state.open_files) { Fail(Error::Busy, "Open save files retain the mount"); }
    if (state.failed) { Fail(Error::Io, "A failed save write/flush prevents transaction publication"); }
    if (state.transaction) {
        auto& transaction = *state.transaction;
        state.committing = true;
        if (state.dirty) {
            if (!transaction.record.prepared) {
                const auto metadata = Metadata(state.path);
                if (Blocks(metadata) != state.blocks) { Fail(Error::Corrupt, "Staged save quota metadata changed"); }
                TouchMetadata(state.path / "sce_sys" / "param.sfo");
                CopyBudget budget{0, state.blocks * kBlock, 0}; ValidateStagedTree(state.path, budget, impl_->checkpoint);
                auto prepared = transaction.record; prepared.prepared = true;
                AtomicWrite(transaction.directory / "state.bin", EncodeRecord(prepared));
                transaction.record = std::move(prepared);
                if (impl_->checkpoint) { impl_->checkpoint("ready"); }
            }
            state.guard.Reset(); transaction.directory_guard.Reset();
            try { FinishPrepared(impl_->root, transaction.directory, transaction.record, impl_->checkpoint); }
            catch (...) { if (Exists(transaction.work)) { state.guard = DirectoryGuard(transaction.work); } throw; }
            state.path = impl_->root / state.name;
            try { CleanupPrepared(impl_->root, transaction.directory, transaction.record); }
            catch (const std::exception& error) {
                std::fprintf(stderr, "Committed save cleanup deferred: %s\n", error.what());
                std::printf("{\"event\":\"native_save_transaction_cleanup_pending\",\"directory\":\"%s\",\"committed\":true}\n", state.name.c_str());
            }
        } else {
            state.guard.Reset(); transaction.directory_guard.Reset(); RemovePlainTree(transaction.directory);
        }
    }
    if (state.transaction) {
        const auto completed = ClockUs();
        std::printf("{\"event\":\"native_save_transaction_completed\",\"directory\":\"%s\",\"action\":\"%s\",\"total_us\":%llu,\"began_clock_us\":%llu,\"clock_us\":%llu,\"thread\":%lu}\n",
            state.name.c_str(), state.dirty ? "commit" : "no_changes", static_cast<unsigned long long>(completed - began),
            static_cast<unsigned long long>(began), static_cast<unsigned long long>(completed), GetCurrentThreadId());
    }
    state.active = false; state.guard.Reset(); state.transaction.reset(); state.lease.Reset(); impl_->mounts.erase(state.name);
}
std::vector<Entry> Store::Search(std::string_view pattern, SortKey key, bool descending) const {
    std::lock_guard lock(impl_->gate); std::vector<Entry> result;
    if (pattern.size() >= 32 || (key != SortKey::Name && key != SortKey::User && key != SortKey::Blocks && key != SortKey::Modified && key != SortKey::Free)) { Fail(Error::Invalid, "Invalid save search options"); }
    std::vector<std::string> listed;
    for (const auto& item : fs::directory_iterator(impl_->root)) {
        const auto name = item.path().filename().string(); if (name.starts_with('.')) { continue; }
        const auto attr = GetFileAttributesW(item.path().c_str()); if (attr == INVALID_FILE_ATTRIBUTES) { WinFail("Save entry disappeared"); }
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT) { Fail(Error::Invalid, "Linked save directory is not searchable"); }
        if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) { continue; }
        SlotName(name); if (!Match(name, pattern)) { continue; }
        if (!fs::exists(item.path() / "sce_sys" / "param.sfo")) { continue; }
        auto path = item.path();
        for (const auto& [mounted_name, weak] : impl_->mounts) {
            if (SlotKey(mounted_name) == SlotKey(name)) { if (const auto mount = weak.lock(); mount && mount->impl_->active) { path = mount->impl_->path; } }
        }
        result.push_back(DescribePath(path, name)); listed.push_back(SlotKey(name)); if (result.size() > 4096) { Fail(Error::Io, "Too many save slots"); }
    }
    for (const auto& [name, weak] : impl_->mounts) {
        if (const auto mount = weak.lock(); mount && mount->impl_->active && Match(name, pattern) &&
            std::find(listed.begin(), listed.end(), SlotKey(name)) == listed.end()) {
            result.push_back(DescribePath(mount->impl_->path, name)); if (result.size() > 4096) { Fail(Error::Io, "Too many save slots"); }
        }
    }
    const auto less = [&](const Entry& a, const Entry& b) {
        switch (key) {
        case SortKey::User: if (a.params.user != b.params.user) { return a.params.user < b.params.user; } break;
        case SortKey::Blocks: if (a.blocks != b.blocks) { return a.blocks < b.blocks; } break;
        case SortKey::Modified: if (a.params.modified != b.params.modified) { return a.params.modified < b.params.modified; } break;
        case SortKey::Free: if (a.free_blocks != b.free_blocks) { return a.free_blocks < b.free_blocks; } break;
        default: break;
        }
        return a.name < b.name;
    };
    std::sort(result.begin(), result.end(), less); if (descending) { std::reverse(result.begin(), result.end()); } return result;
}
Entry Store::Describe(const std::shared_ptr<Mount>& mount) const {
    std::lock_guard lock(impl_->gate);
    if (!mount || mount->impl_->store != impl_ || !mount->impl_->active) { Fail(Error::NotMounted, "Save mount is not active"); }
    return DescribePath(mount->impl_->path, mount->impl_->name);
}
void Store::SetParams(const std::shared_ptr<Mount>& mount, const Params& params) {
    std::lock_guard lock(impl_->gate);
    if (!mount || mount->impl_->store != impl_ || !mount->impl_->active) { Fail(Error::NotMounted, "Save mount is not active"); }
    if (mount->impl_->readonly) { Fail(Error::ReadOnly, "Save mount is read only"); }
    if (mount->impl_->committing) { Fail(Error::Busy, "Save transaction publication has started"); }
    if (params.title.size() >= 128 || params.subtitle.size() >= 128 || params.detail.size() >= 1024) { Fail(Error::Invalid, "Save description is too long"); }
    const auto path = mount->impl_->path; auto metadata = Metadata(path);
    try {
        metadata.SetText("MAINTITLE", params.title); metadata.SetText("SUBTITLE", params.subtitle); metadata.SetText("DETAIL", params.detail); metadata.SetInteger("SAVEDATA_LIST_PARAM", static_cast<std::int32_t>(params.user));
    } catch (const std::runtime_error&) { Fail(Error::Invalid, "Invalid save description encoding"); }
    const auto bytes = metadata.Serialize(); const auto old_size = fs::file_size(path / "sce_sys" / "param.sfo"); const auto used = Used(path);
    if (used < old_size || used - old_size + bytes.size() > mount->impl_->blocks * kBlock) { Fail(Error::NoSpace, "Save metadata exceeds quota"); }
    AtomicWrite(path / "sce_sys" / "param.sfo", bytes); mount->impl_->dirty = true;
}
void Store::SaveIcon(const std::shared_ptr<Mount>& mount, std::span<const std::byte> bytes) {
    std::lock_guard lock(impl_->gate);
    if (!mount || mount->impl_->store != impl_ || !mount->impl_->active) { Fail(Error::NotMounted, "Save mount is not active"); }
    if (mount->impl_->readonly) { Fail(Error::ReadOnly, "Save mount is read only"); }
    if (mount->impl_->committing) { Fail(Error::Busy, "Save transaction publication has started"); }
    const auto path = mount->impl_->path; const auto icon = path / "sce_sys" / "icon0.png";
    const auto old_size = fs::exists(icon) ? fs::file_size(icon) : 0; const auto used = Used(path);
    if (bytes.empty() || bytes.size() > 16 * 1024 * 1024) { Fail(Error::Invalid, "Invalid save icon size"); }
    if (used < old_size || used - old_size + bytes.size() > mount->impl_->blocks * kBlock) { Fail(Error::NoSpace, "Save icon exceeds quota"); }
    AtomicWrite(icon, bytes); mount->impl_->dirty = true;
}
std::unique_ptr<File> Store::Open(const std::shared_ptr<Mount>& mount, std::string_view relative, OpenOptions options) {
    const auto part = Relative(relative); std::lock_guard lock(impl_->gate);
    if (!mount || mount->impl_->store != impl_ || !mount->impl_->active) { Fail(Error::NotMounted, "Save mount is not active"); }
    if (mount->impl_->committing) { Fail(Error::Busy, "Save transaction publication has started"); }
    if ((!options.read && !options.write) || (options.truncate && !options.write) || (options.exclusive && !options.create)) { Fail(Error::Invalid, "Invalid save open options"); }
    if (options.directory && (options.write || options.create || options.truncate)) { Fail(Error::Invalid, "Directory access cannot create or truncate data files"); }
    if (mount->impl_->readonly && (options.write || options.create || options.truncate)) { Fail(Error::ReadOnly, "Save mount is read only"); }
    const auto slot = mount->impl_->path; const auto path = part.empty() ? slot : slot / part;
    auto file = std::unique_ptr<File>(new File(nullptr));
    auto state = std::make_unique<File::Impl>(); state->mount = mount; state->options = options;
    state->guards = GuardParents(part.empty() ? slot : path.parent_path());
    // Inspect the opened identity before any destructive truncation. A hard
    // link placed in a profile must not turn an unrelated file into save data.
    const auto disposition = options.create ? (options.exclusive ? CREATE_NEW : OPEN_ALWAYS) : OPEN_EXISTING;
    // Creation needs a flush-capable handle even when the guest requested a
    // read-only descriptor. Guest writes remain governed by options.write.
    const auto access = (options.read ? GENERIC_READ : 0) | ((options.write || options.create) ? GENERIC_WRITE : 0);
    if (part.empty() && (options.create || options.truncate || options.write)) { Fail(Error::Directory, "Cannot create or overwrite save mount root"); }
    state->handle = Handle(CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, disposition,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS | (options.sync ? FILE_FLAG_WRITE_THROUGH : 0), nullptr));
    if (state->handle.value == INVALID_HANDLE_VALUE) { WinFail("Cannot open mounted save file"); }
    const auto info = Node(state->handle.value);
    if (options.directory && !info.directory) { Fail(Error::NotDirectory, "Save file is not a directory"); }
    if (info.directory && options.write) { Fail(Error::Directory, "Cannot open directory for data writes"); }
    if (options.write && info.links != 1) { Fail(Error::Permission, "Hard-linked save files are not writable"); }
    if (options.truncate) {
        LARGE_INTEGER start{};
        if (!SetFilePointerEx(state->handle.value, start, nullptr, FILE_BEGIN) || !SetEndOfFile(state->handle.value)) { WinFail("Cannot truncate save file"); }
    }
    if (options.create || options.truncate) { mount->impl_->dirty = state->dirty = true; }
    file->impl_ = std::move(state); ++mount->impl_->open_files; return file;
}
void Store::Delete(std::string_view name) {
    SlotName(name); std::lock_guard lock(impl_->gate);
    for (const auto& [other, weak] : impl_->mounts) { if (const auto mount = weak.lock(); mount && mount->impl_->active &&
        CompareStringOrdinal(fs::path(other).c_str(), -1, fs::path(name).c_str(), -1, TRUE) == CSTR_EQUAL) { Fail(Error::Busy, "Mounted save slot cannot be removed"); } }
    auto lease = SlotLease(impl_->root, name); RecoverSlot(impl_->root, SlotKey(name));
    // Broken metadata must not prevent recoverable removal of a damaged slot.
    // Only the plain slot directory is moved; no child is followed or deleted.
    const auto target = impl_->root / std::string(name); { auto guard = DirectoryGuard(target); }
    const auto trash = impl_->root / ".trash";
    if (!CreateDirectoryW(trash.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) { WinFail("Cannot create save recovery directory"); }
    auto guard = DirectoryGuard(trash); const auto destination = trash / (fs::path(name).wstring() + L"-" + Unique(L"removed-"));
    if (!MoveFileExW(target.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) { WinFail("Cannot move removed save to recovery directory"); }
}
} // namespace kt::save
