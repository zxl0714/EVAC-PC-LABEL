#include "content_view.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cwctype>
#include <limits>
#include <stdexcept>

namespace kt::content {
namespace {
namespace fs = std::filesystem;
[[noreturn]] void Invalid(const char* message) { throw std::runtime_error(message); }
bool CheckPath(const fs::path& path) {
    std::vector<fs::path> ancestors;
    for (auto current = path; !current.empty();) {
        ancestors.push_back(current);
        const auto parent = current.parent_path(); if (parent == current) { break; } current = parent;
    }
    std::reverse(ancestors.begin(), ancestors.end());
    for (std::size_t i = 0; i < ancestors.size(); ++i) {
        const auto attributes = GetFileAttributesW(ancestors[i].c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) { return false; }
            Invalid("Cannot inspect content path");
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) { Invalid("Reparse points are not supported in content paths"); }
        if (i + 1 < ancestors.size() && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) { throw NotDirectory("Content path parent is not a directory"); }
    }
    return true;
}
fs::path Root(const fs::path& path) {
    if (path.empty() || !path.is_absolute() || !CheckPath(path) || !fs::is_directory(path)) { Invalid("Content root must be an existing absolute directory"); }
    return fs::canonical(path);
}
fs::path Relative(std::string_view input) {
    const auto reject = [](const char* message) { throw InvalidPath(message); };
    if (input.empty() || input.size() > 4096) { reject("Invalid virtual content path"); }
    std::wstring wide(input.size(), L'\0');
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), wide.data(), static_cast<int>(wide.size()));
    if (!count) { reject("Invalid UTF-8 content path"); } wide.resize(count);
    std::size_t start = 0;
    while (start < wide.size()) {
        const auto slash = wide.find(L'/', start), end = slash == std::wstring::npos ? wide.size() : slash;
        const auto part = wide.substr(start, end - start);
        if (part.empty() || part == L"." || part == L".." || part.size() > 255 || part.back() == L'.' || part.back() == L' ') { reject("Invalid content path component"); }
        for (const auto c : part) { if (c < 32 || std::wstring_view(L"\\:*?\"<>|").find(c) != std::wstring_view::npos) { reject("Invalid content path character"); } }
        auto stem = part.substr(0, part.find(L'.'));
        for (auto& c : stem) { c = static_cast<wchar_t>(std::towupper(c)); }
        if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" || stem == L"CONIN$" || stem == L"CONOUT$" ||
            (stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) && stem[3] >= L'1' && stem[3] <= L'9')) { reject("Device names are not content files"); }
        if (slash == std::wstring::npos) { break; }
        start = slash + 1; if (start == wide.size()) { reject("Trailing content path separator"); }
    }
    auto result = fs::path(wide);
    if (result.is_absolute() || result.has_root_name() || result.has_root_directory()) { reject("Absolute host content paths are not accepted"); }
    return result;
}
std::unique_ptr<ReadOnlyFile> Open(const fs::path& root, const fs::path& relative, bool allow_directory = false) {
    const auto path = relative.empty() ? root : root / relative;
    if (!CheckPath(path)) { return {}; }
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) { Invalid("Cannot open existing content file read-only"); }
    struct Guard { HANDLE value; ~Guard() { if (value != INVALID_HANDLE_VALUE) { CloseHandle(value); } } } guard{handle};
    BY_HANDLE_FILE_INFORMATION info{}; FILE_BASIC_INFO basic{}; FILE_STANDARD_INFO standard{};
    if (!GetFileInformationByHandle(handle, &info) || !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) ||
        !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
        standard.EndOfFile.QuadPart < 0 || standard.AllocationSize.QuadPart < 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || (!allow_directory && standard.Directory)) { Invalid("Invalid content file handle"); }
    const auto needed = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!needed || needed > 32768) { Invalid("Cannot resolve content file handle"); }
    std::wstring final(needed, L'\0');
    const auto length = GetFinalPathNameByHandleW(handle, final.data(), needed, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= needed) { Invalid("Cannot resolve content file handle"); } final.resize(length);
    if (final.starts_with(L"\\\\?\\UNC\\")) { final = L"\\\\" + final.substr(8); }
    else if (final.starts_with(L"\\\\?\\")) { final.erase(0, 4); }
    if (_wcsicmp(final.c_str(), path.lexically_normal().c_str()) != 0) { Invalid("Content file resolved outside its selected path"); }
    const NodeInfo node{standard.Directory != FALSE, info.dwVolumeSerialNumber, info.nFileIndexLow, info.nNumberOfLinks,
        standard.Directory ? 0 : static_cast<std::uint64_t>(standard.EndOfFile.QuadPart), static_cast<std::uint64_t>(standard.AllocationSize.QuadPart),
        static_cast<std::uint64_t>(basic.LastAccessTime.QuadPart), static_cast<std::uint64_t>(basic.LastWriteTime.QuadPart),
        static_cast<std::uint64_t>(basic.ChangeTime.QuadPart), static_cast<std::uint64_t>(basic.CreationTime.QuadPart)};
    auto file = std::make_unique<ReadOnlyFile>(handle, node); guard.value = INVALID_HANDLE_VALUE; return file;
}
Sfo ReadSfo(const fs::path& root) {
    auto file = Open(root, fs::path(L"sce_sys") / L"param.sfo");
    if (!file || file->Size() > 1024 * 1024) { Invalid("Missing or oversized content metadata"); }
    std::vector<std::byte> bytes(static_cast<std::size_t>(file->Size()));
    if (file->ReadAt(0, bytes) != bytes.size()) { Invalid("Incomplete content metadata"); }
    return Sfo::Parse(bytes);
}
std::string Required(const Sfo& sfo, std::string_view key) {
    const auto value = sfo.Text(key); if (!value) { Invalid("Missing or wrongly typed content metadata"); } return *value;
}
void Identity(std::string_view title, std::string_view content) {
    if (title.size() != 9 || !title.starts_with("CUSA") || !std::all_of(title.begin() + 4, title.end(), [](char c) { return c >= '0' && c <= '9'; })) { Invalid("Invalid title ID"); }
    if (content.size() != 36 || content[6] != '-' || content.substr(7, 9) != title || content[16] != '_' || content[19] != '-') { Invalid("Invalid content ID"); }
    for (std::size_t i = 0; i < content.size(); ++i) {
        if (i == 6 || i == 16 || i == 19) { continue; }
        const auto c = content[i]; if (!(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_') { Invalid("Invalid content ID character"); }
    }
}
}
ReadOnlyFile::~ReadOnlyFile() { CloseHandle(handle_); }
std::size_t ReadOnlyFile::ReadAt(std::uint64_t offset, std::span<std::byte> output) {
    std::unique_lock lock(mutex_);
    if (info_.directory) { Invalid("Cannot read content directory as a file"); }
    if (offset >= info_.size || output.empty()) { return 0; }
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN)) { Invalid("Content seek failed"); }
    const auto wanted = static_cast<std::size_t>((std::min)(info_.size - offset, static_cast<std::uint64_t>(output.size())));
    std::size_t total = 0;
    while (total < wanted) {
        const auto chunk = static_cast<DWORD>((std::min)(wanted - total, std::size_t{1} << 30)); DWORD received{};
        if (!ReadFile(handle_, output.data() + total, chunk, &received, nullptr)) { Invalid("Content read failed"); }
        total += received; if (received != chunk) { break; }
    }
    struct Applied { std::uint64_t offset,bytes; };
    std::array<Applied,16> applied; std::size_t applied_count{}; unsigned bit=1;
    if (read_patches_.size()>applied.size()) { Invalid("Too many content read overlays"); }
    for (const auto& patch : read_patches_) {
        const auto begin = (std::max)(offset, patch.offset);
        const auto end = (std::min)(offset + total, patch.offset + patch.replacement.size());
        if (begin < end) {
            std::memcpy(output.data() + static_cast<std::size_t>(begin - offset),
                patch.replacement.data() + static_cast<std::size_t>(begin - patch.offset), static_cast<std::size_t>(end - begin));
            if (!(reported_patches_ & bit)) {
                reported_patches_ |= bit;
                applied[applied_count++]={patch.offset,patch.replacement.size()};
            }
        }
        bit <<= 1;
    }
    lock.unlock();
    // Installation validates originals but does not prove a runtime read.
    // Report each applied range once per file handle, outside its I/O lock.
    for (std::size_t i=0;i<applied_count;++i)
        std::printf("{\"event\":\"native_content_read_overlay_applied\",\"archive_bytes\":%llu,\"patch_offset\":%llu,\"patch_bytes\":%llu,\"read_offset\":%llu,\"read_bytes\":%zu}\n",
                    static_cast<unsigned long long>(info_.size),static_cast<unsigned long long>(applied[i].offset),
                    static_cast<unsigned long long>(applied[i].bytes),static_cast<unsigned long long>(offset),total);
    return total;
}
void ReadOnlyFile::InstallReadPatches(std::shared_ptr<const ReadPatchSet> patches, std::string_view relative) {
    if (!read_patches_.empty()) { Invalid("Read overlays are immutable after file setup"); }
    std::vector<ReadPatch> selected;
    for (const auto& patch : patches->entries) {
        if (patch.path != relative) { continue; }
        if (info_.directory || info_.size != patch.file_size) { Invalid("Read overlay original file size differs"); }
        std::vector<std::byte> original(patch.replacement.size());
        if (ReadAt(patch.offset, original) != original.size() || ReadPatchHash(original) != patch.original_sha256) {
            Invalid("Read overlay original bytes differ from their expected SHA-256");
        }
        selected.push_back(patch);
    }
    read_patches_ = std::move(selected);
}
ContentView::ContentView(const fs::path& app, const fs::path& patch, const fs::path& addons, const fs::path& read_patches)
    : app_(Root(app)), metadata_(ReadSfo(app_)) {
    title_ = Required(metadata_, "TITLE_ID"); content_ = Required(metadata_, "CONTENT_ID"); Identity(title_, content_);
    if (Required(metadata_, "CATEGORY") != "gd") { Invalid("The app root must contain a base application"); }
    if (!patch.empty()) {
        patch_ = Root(patch); const auto metadata = ReadSfo(patch_);
        if (Required(metadata, "CATEGORY") != "gp" || Required(metadata, "TITLE_ID") != title_ || Required(metadata, "CONTENT_ID") != content_) { Invalid("Patch does not belong to this application"); }
        if (const auto type = metadata.Integer("APP_TYPE"); type && type != metadata_.Integer("APP_TYPE")) { Invalid("Patch application type differs from base"); }
        metadata_.Overlay(metadata);
    }
    if (!addons.empty()) {
        const auto addon_root = Root(addons);
        std::vector<fs::path> paths;
        for (const auto& entry : fs::directory_iterator(addon_root)) {
            if (!CheckPath(entry.path())) { Invalid("Additional content disappeared during enumeration"); }
            if (entry.is_directory()) { paths.push_back(entry.path()); }
            if (paths.size() > 64) { Invalid("Too many additional content directories"); }
        }
        std::sort(paths.begin(), paths.end());
        for (const auto& path : paths) {
            const auto root = Root(path); const auto metadata = ReadSfo(root);
            const auto title_id = Required(metadata, "TITLE_ID"), content_id = Required(metadata, "CONTENT_ID"); Identity(title_id, content_id);
            if (title_id != title_ || Required(metadata, "CATEGORY") != "ac") { Invalid("Unsupported or mismatched additional content"); }
            const auto label = content_id.substr(20);
            if (std::any_of(addons_.begin(), addons_.end(), [&](const auto& item) { return item.label == label; })) { Invalid("Duplicate additional content label"); }
            addons_.push_back({label, root});
        }
    }
    // This title stores both optional modes in /app0. Its NoExtraData path
    // interprets the 16-byte field as a NUL-terminated local capability name.
    // These runtime labels are not PSN content IDs or network credentials.
    if (title_ == "CUSA09062" && content_ == "JP0092-CUSA09062_00-KETSUIDEATHTINY0") {
        constexpr std::array labels{"KETSUIBUILTIN001", "KETSUIBUILTIN002"};
        constexpr std::array capabilities{"KETSUI_PS4_DLC1", "KETSUI_PS4_DLC2"};
        static_assert(sizeof("KETSUIBUILTIN001") == 17 && sizeof("KETSUI_PS4_DLC1") == 16);
        for (std::size_t i = 0; i < labels.size(); ++i) {
            if (std::any_of(addons_.begin(), addons_.end(), [&](const auto& item) { return item.label == labels[i]; })) {
                Invalid("Additional content uses a reserved built-in label");
            }
            Addon item{labels[i], {}, AddonStatus::NoExtraData};
            std::memcpy(item.capability.data(), capabilities[i], item.capability.size());
            addons_.push_back(std::move(item));
        }
    }
    if (!read_patches.empty()) {
        auto prepared = std::make_shared<ReadPatchSet>(ReadPatchSet::Load(read_patches));
        if (prepared->title != title_ || metadata_.Text("APP_VER") != prepared->version) { Invalid("Read overlay application identity differs"); }
        read_patches_ = std::move(prepared);
        // Validate every target before entering original code. A fresh open
        // also verifies the original range, so later content changes fail closed.
        std::string previous;
        for (const auto& item : read_patches_->entries) {
            if (item.path == previous) { continue; }
            if (!OpenApp0(item.path)) { Invalid("Read overlay original file is missing"); }
            previous = item.path;
        }
    }
}
std::unique_ptr<ReadOnlyFile> ContentView::OpenApp0(std::string_view path, bool allow_directory) const {
    const auto relative = path.empty() && allow_directory ? fs::path{} : Relative(path);
    std::unique_ptr<ReadOnlyFile> file;
    if (!patch_.empty()) { file = Open(patch_, relative, allow_directory); }
    if (!file) { file = Open(app_, relative, allow_directory); }
    if (file && read_patches_) {
        const auto name = relative.generic_u8string();
        file->InstallReadPatches(read_patches_, {reinterpret_cast<const char*>(name.data()), name.size()});
    }
    return file;
}
std::unique_ptr<ReadOnlyFile> ContentView::OpenAddon(std::size_t index, std::string_view path, bool allow_directory) const {
    if (index >= addons_.size() || addons_[index].status != AddonStatus::Installed) { return {}; }
    return Open(addons_[index].root, path.empty() && allow_directory ? fs::path{} : Relative(path), allow_directory);
}
bool ContentView::EntitlementKey(std::size_t index, std::array<std::byte, 16>& output) const {
    if (index >= addons_.size()) { return false; }
    if (addons_[index].status == AddonStatus::NoExtraData) { output = addons_[index].capability; return true; }
    auto file = Open(addons_[index].root, L"entitlement.key");
    if (!file) { return false; }
    if (file->Size() != output.size()) { Invalid("Entitlement key must contain exactly 16 bytes"); }
    std::array<std::byte, 16> value{};
    if (file->ReadAt(0, value) != value.size()) { Invalid("Incomplete entitlement key"); }
    output = value; return true;
}
}
