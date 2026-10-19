#include "native_files.h"
#include "native_content.h"
#include "native_diagnostics.h"
#include "native_storage.h"
#include "native_memory.h"
#include "memory/direct_fixture.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
constexpr int Error(unsigned code) { return static_cast<int>(0x80020000u | code); }
constexpr int kNoEntry = Error(2), kBadFd = Error(9), kNoMemory = Error(12), kFault = Error(14);
constexpr int kNotDirectory = Error(20), kIsDirectory = Error(21), kInvalid = Error(22);
constexpr int kTooMany = Error(24), kReadOnly = Error(30), kNameTooLong = Error(63), kOverflow = Error(84);
constexpr int kDirectory = 0x20000;
struct Timespec { std::int64_t seconds, nanoseconds; };
struct FileStat {
    std::uint32_t device, inode;
    std::uint16_t mode, links;
    std::uint32_t uid, gid, rdev;
    Timespec accessed, modified, changed;
    std::int64_t size, blocks;
    std::uint32_t block_size, flags, generation;
    std::int32_t spare;
    Timespec created;
};
static_assert(sizeof(FileStat) == 120 && offsetof(FileStat, accessed) == 24 && offsetof(FileStat, size) == 72 && offsetof(FileStat, created) == 104);
struct Descriptor {
    std::mutex mutex;
    std::unique_ptr<kt::content::ReadOnlyFile> file;
    std::unique_ptr<kt::save::File> save;
    std::int64_t position = 0;
    bool closed = false;
};
std::mutex descriptor_mutex;
// A concurrent-open limit, not a process-lifetime limit: closed slots are reused.
std::array<std::shared_ptr<Descriptor>, 1024> descriptors;
std::array<bool, 1024> reserved_descriptors{};
std::atomic<std::uint64_t> path_log_count{}, read_log_count{}, write_log_count{};
struct PathError { int code; };
int SaveFileError(kt::save::Error error) {
    using E = kt::save::Error;
    switch (error) {
    case E::NotFound: case E::NotMounted: return kNoEntry;
    case E::Exists: return Error(17);
    case E::Busy: return Error(16);
    case E::NoSpace: return Error(28);
    case E::ReadOnly: return kReadOnly;
    case E::BadFile: return kBadFd;
    case E::Directory: return kIsDirectory;
    case E::NotDirectory: return kNotDirectory;
    case E::Invalid: return kInvalid;
    case E::Permission: return Error(13);
    default: return Error(5);
    }
}
struct DescriptorReservation {
    int fd = -1;
    DescriptorReservation() {
        std::lock_guard lock(descriptor_mutex);
        for (std::size_t index = 3; index < descriptors.size(); ++index) {
            if (!descriptors[index] && !reserved_descriptors[index]) { fd = static_cast<int>(index); reserved_descriptors[index] = true; return; }
        }
        throw PathError{kTooMany};
    }
    ~DescriptorReservation() { if (fd >= 0) { std::lock_guard lock(descriptor_mutex); reserved_descriptors[fd] = false; } }
    int Publish(std::shared_ptr<Descriptor> descriptor) {
        std::lock_guard lock(descriptor_mutex); const auto result = fd;
        descriptors[fd] = std::move(descriptor); reserved_descriptors[fd] = false; fd = -1; return result;
    }
};
kt::content::NodeInfo Info(const Descriptor& descriptor) { return descriptor.save ? descriptor.save->Info() : descriptor.file->Info(); }

std::string Quote(std::string_view text) {
    constexpr char hex[] = "0123456789abcdef";
    std::string output = "\"";
    for (const auto c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '\\' || c == '"') { output += '\\'; output += c; }
        else if (byte < 32) { output += "\\u00"; output += hex[byte >> 4]; output += hex[byte & 15]; }
        else { output += c; }
    }
    output += '"'; return output;
}
bool PathLog(const char* api, std::string_view path, int flags = 0) {
    const auto index = path_log_count.fetch_add(1, std::memory_order_relaxed);
    if (index < 128) {
        std::printf("{\"event\":\"native_file_request\",\"api\":\"%s\",\"path\":%s,\"flags\":%d}\n", api, Quote(path).c_str(), flags);
        return true;
    }
    if (index == 128) { std::puts("{\"event\":\"native_file_trace_limit\",\"kind\":\"request\",\"limit\":128}"); }
    return false;
}
template<typename Function> __declspec(noinline) auto Api(const void* caller, Function&& function) -> decltype(function()) {
    try { return function(); }
    catch (const PathError& error) { return error.code; }
    catch (const kt::save::Failure& error) { return SaveFileError(error.error); }
    catch (const kt::content::InvalidPath&) { return kInvalid; }
    catch (const kt::content::NotDirectory&) { return kNotDirectory; }
    catch (const std::bad_alloc&) { return kNoMemory; }
    catch (...) { UnsupportedApi("native_file_host_or_namespace_failure", 0, caller); }
}
int Path(const char* input, std::string_view& output) {
    if (!input) { return kFault; }
    std::size_t size = 0;
    while (size <= 255 && input[size]) { ++size; }
    if (size > 255) { return kNameTooLong; }
    output = {input, size};
    if (output.empty() || output[0] != '/') { return kInvalid; }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, static_cast<int>(size), nullptr, 0)) { return kInvalid; }
    return 0;
}
std::unique_ptr<kt::content::ReadOnlyFile> Node(std::string_view path) {
    // Accept a single trailing directory separator, without accepting empty
    // intermediate components, Windows aliases, dot traversal, or host paths.
    const bool trailing = path.size() > 1 && path.back() == '/';
    if (trailing) { path.remove_suffix(1); }
    if (path.size() > 1 && path.back() == '/') { throw kt::content::InvalidPath("Empty directory path component"); }
    auto file = OpenNativeContentFile(path, true);
    if (trailing && file && !file->Info().directory) { throw PathError{kNotDirectory}; }
    return file;
}
std::shared_ptr<Descriptor> Find(int fd) {
    std::lock_guard lock(descriptor_mutex);
    return fd >= 3 && fd < static_cast<int>(descriptors.size()) ? descriptors[fd] : nullptr;
}
int Reach(const char* name, const void* caller) {
    return Api(caller, [&] {
        std::string_view path; if (const auto error = Path(name, path)) { return error; }
        if (PathLog("sceKernelCheckReachability", path)) { TraceNativeApi("sceKernelCheckReachability", path.size(), caller); }
        if (path == "/" && NativeContentConfigured()) { return 0; }
        if (NativeSavePath(path)) { auto file = OpenNativeSaveFile(path, {.read = true}); return file ? 0 : kNoEntry; }
        return Node(path) ? 0 : kNoEntry;
    });
}
int Open(const char* name, int flags, unsigned short /*mode*/, const void* caller) {
    return Api(caller, [&] {
        std::string_view path; if (const auto error = Path(name, path)) { return error; }
        const bool trace = PathLog("sceKernelOpen", path, flags);
        if ((flags & 3) == 3 || flags < 0) { return kInvalid; }
        constexpr int known = 3 | 4 | 8 | 0x80 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x10000 | kDirectory;
        if (flags & ~known) { UnsupportedApi("sceKernelOpen_flags", static_cast<unsigned>(flags), caller); }
        // Reserve a descriptor before CREATE/TRUNCATE can modify a save file.
        // An unpublished reservation is not visible to concurrent readers.
        auto descriptor = std::make_shared<Descriptor>(); DescriptorReservation reservation;
        if (NativeSavePath(path)) {
            if (flags & 0x10000) { UnsupportedApi("sceKernelOpen_save_direct_io", static_cast<unsigned>(flags), caller); }
            descriptor->save = OpenNativeSaveFile(path, {.read = (flags & 3) != 1, .write = (flags & 3) != 0,
                .create = (flags & 0x200) != 0, .truncate = (flags & 0x400) != 0, .exclusive = (flags & 0x800) != 0,
                .append = (flags & 8) != 0, .sync = (flags & 0x80) != 0, .directory = (flags & kDirectory) != 0});
        } else {
            // Original content never gains write capability from save support.
            auto file = Node(path);
            if ((flags & 3) || (flags & (0x200 | 0x400))) { return kReadOnly; }
            if (flags & (8 | 0x800 | 0x10000)) { UnsupportedApi("sceKernelOpen_read_flags", static_cast<unsigned>(flags), caller); }
            if (!file) { return kNoEntry; }
            if ((flags & kDirectory) && !file->Info().directory) { return kNotDirectory; }
            descriptor->file = std::move(file);
        }
        const auto fd = reservation.Publish(std::move(descriptor));
        if (trace) { try { TraceNativeApi("sceKernelOpen", static_cast<unsigned>(fd), caller); } catch (...) { std::puts("{\"event\":\"native_file_trace_dropped\"}"); } }
        return fd;
    });
}
int Close(int fd) {
    std::shared_ptr<Descriptor> descriptor;
    {
        std::lock_guard lock(descriptor_mutex);
        if (fd < 3 || fd >= static_cast<int>(descriptors.size()) || !descriptors[fd]) { return kBadFd; }
        descriptor = std::move(descriptors[fd]);
    }
    // No registry lock during IO or waiting for an in-flight read. A reader
    // holding the old shared descriptor cannot access a reused descriptor slot.
    std::lock_guard lock(descriptor->mutex);
    descriptor->closed = true; int status{};
    if (descriptor->save) { try { descriptor->save->Flush(); } catch (const kt::save::Failure& error) { status = SaveFileError(error.error); } }
    descriptor->save.reset(); descriptor->file.reset(); return status;
}
std::int64_t Read(int fd, void* output, std::uint64_t count, std::int64_t offset, bool positional, const void* caller) {
    return Api(caller, [&]() -> std::int64_t {
        const auto descriptor = Find(fd); if (!descriptor) { return kBadFd; }
        if (count > static_cast<std::uint64_t>(INT64_MAX) || (positional && offset < 0)) { return kInvalid; }
        if (!output && count) { return kFault; }
        std::lock_guard lock(descriptor->mutex);
        if (descriptor->closed) { return kBadFd; }
        if (Info(*descriptor).directory) { return kIsDirectory; }
        const auto at = positional ? offset : descriptor->position;
        // Reading beyond EOF is valid, but the resulting logical offset must
        // remain representable by the guest's signed 64-bit off_t.
        const auto available = static_cast<std::uint64_t>(INT64_MAX - at);
        const auto buffer = std::span{static_cast<std::byte*>(output), static_cast<std::size_t>((std::min)(count, available))};
        auto destination = BeginNativeHostWrite(output, buffer.size());
        if (!destination) { return kFault; }
        const auto received = descriptor->save ? descriptor->save->ReadAt(static_cast<std::uint64_t>(at), buffer) : descriptor->file->ReadAt(static_cast<std::uint64_t>(at), buffer);
        if (!positional) { descriptor->position += static_cast<std::int64_t>(received); }
        const auto read_index = read_log_count.fetch_add(1, std::memory_order_relaxed);
        if (read_index < 64) {
            std::printf("{\"event\":\"native_file_read\",\"fd\":%d,\"offset\":%lld,\"requested\":%llu,\"received\":%zu,\"positional\":%s}\n",
                fd, at, count, received, positional ? "true" : "false");
        }
        else if (read_index == 64) { std::puts("{\"event\":\"native_file_trace_limit\",\"kind\":\"read\",\"limit\":64}"); }
        return static_cast<std::int64_t>(received);
    });
}
std::int64_t Write(int fd, const void* input, std::uint64_t count, const void* caller) {
    return Api(caller, [&]() -> std::int64_t {
        const auto descriptor = Find(fd); if (!descriptor) { return kBadFd; }
        if (count > static_cast<std::uint64_t>(INT64_MAX)) { return kInvalid; }
        if (!input && count) { return kFault; }
        std::lock_guard lock(descriptor->mutex);
        if (descriptor->closed || !descriptor->save) { return kBadFd; }
        std::uint64_t end{}; const auto written = descriptor->save->WriteAt(static_cast<std::uint64_t>(descriptor->position),
            {static_cast<const std::byte*>(input), static_cast<std::size_t>(count)}, end);
        if (end > INT64_MAX) { return kOverflow; } descriptor->position = static_cast<std::int64_t>(end);
        const auto index = write_log_count.fetch_add(1, std::memory_order_relaxed);
        if (index < 64) { std::printf("{\"event\":\"native_file_write\",\"fd\":%d,\"requested\":%llu,\"written\":%zu,\"position\":%llu,\"save_mount\":true}\n", fd, count, written, end); }
        return static_cast<std::int64_t>(written);
    });
}
std::int64_t Seek(int fd, std::int64_t offset, int whence) {
    return Api(nullptr, [&]() -> std::int64_t {
    const auto descriptor = Find(fd); if (!descriptor) { return kBadFd; }
    std::lock_guard lock(descriptor->mutex);
    if (descriptor->closed) { return kBadFd; }
    const auto info = Info(*descriptor); if (info.directory) { return kInvalid; }
    std::int64_t base = 0;
    if (whence == 1) { base = descriptor->position; }
    else if (whence == 2) { base = static_cast<std::int64_t>(info.size); }
    else if (whence != 0) { return kInvalid; }
    if (offset < -base) { return kInvalid; }
    if (offset > INT64_MAX - base) { return kOverflow; }
    descriptor->position = base + offset; return descriptor->position;
    });
}
Timespec Time(std::uint64_t ticks) {
    // FILETIME's 1601 epoch and 100 ns units -> Unix epoch, normalized nsec.
    constexpr std::uint64_t epoch = 116444736000000000ull, second = 10000000;
    if (ticks >= epoch) { const auto delta = ticks - epoch; return {static_cast<std::int64_t>(delta / second), static_cast<std::int64_t>(delta % second) * 100}; }
    const auto delta = epoch - ticks, remainder = delta % second;
    return {-static_cast<std::int64_t>(delta / second) - (remainder ? 1 : 0), remainder ? static_cast<std::int64_t>(second - remainder) * 100 : 0};
}
int FillStat(const kt::content::NodeInfo& info, FileStat* output, bool writable = false) {
    if (info.links > UINT16_MAX || info.size > INT64_MAX || info.allocation / 512 > INT64_MAX) { return kOverflow; }
    FileStat result{};
    result.device = info.device; result.inode = info.inode; result.links = static_cast<std::uint16_t>(info.links);
    // Permissions describe the read-only virtual mount, not Windows ACLs.
    result.mode = static_cast<std::uint16_t>(info.directory ? (writable ? 0040755 : 0040555) : (writable ? 0100644 : 0100444));
    result.accessed = Time(info.access_time); result.modified = Time(info.modification_time);
    result.changed = Time(info.change_time); result.created = Time(info.creation_time);
    result.size = static_cast<std::int64_t>(info.size); result.blocks = static_cast<std::int64_t>(info.allocation / 512 + (info.allocation % 512 ? 1 : 0));
    result.block_size = 4096; *output = result; return 0;
}
int Stat(const char* name, FileStat* output, const void* caller) {
    return Api(caller, [&] {
        if (!output) { return kFault; }
        std::string_view path; if (const auto error = Path(name, path)) { return error; }
        PathLog("sceKernelStat", path);
        if (NativeSavePath(path)) { const auto file = OpenNativeSaveFile(path, {.read = true}); return FillStat(file->Info(), output, !file->ReadOnlyMount()); }
        const auto file = Node(path); return file ? FillStat(file->Info(), output) : kNoEntry;
    });
}
int Fstat(int fd, FileStat* output) {
    return Api(nullptr, [&] {
    if (!output) { return kFault; }
    const auto descriptor = Find(fd); if (!descriptor) { return kBadFd; }
    std::lock_guard lock(descriptor->mutex);
    return descriptor->closed ? kBadFd : FillStat(Info(*descriptor), output, descriptor->save && !descriptor->save->ReadOnlyMount());
    });
}
int KT_SYSV KernelReach(const char* path) { return Reach(path, __builtin_return_address(0)); }
int KT_SYSV KernelOpen(const char* path, int flags, unsigned short mode) { return Open(path, flags, mode, __builtin_return_address(0)); }
int KT_SYSV KernelClose(int fd) { return Close(fd); }
std::int64_t KT_SYSV KernelRead(int fd, void* data, std::uint64_t size) { return Read(fd, data, size, 0, false, __builtin_return_address(0)); }
std::int64_t KT_SYSV KernelWrite(int fd, const void* data, std::uint64_t size) { return Write(fd, data, size, __builtin_return_address(0)); }
std::int64_t KT_SYSV KernelPread(int fd, void* data, std::uint64_t size, std::int64_t offset) { return Read(fd, data, size, offset, true, __builtin_return_address(0)); }
std::int64_t KT_SYSV KernelSeek(int fd, std::int64_t offset, int whence) { return Seek(fd, offset, whence); }
int KT_SYSV KernelStat(const char* path, FileStat* output) { return Stat(path, output, __builtin_return_address(0)); }
int KT_SYSV KernelFstat(int fd, FileStat* output) { return Fstat(fd, output); }
}

void PrintNativeFileDiagnostics() {
    const auto requests = path_log_count.load(std::memory_order_relaxed), reads = read_log_count.load(std::memory_order_relaxed);
    if (!requests && !reads) { return; }
    std::printf("{\"event\":\"native_file_diagnostics\",\"requests\":%llu,\"successful_reads\":%llu,\"request_details_omitted\":%llu,\"read_details_omitted\":%llu}\n",
        requests, reads, requests > 128 ? requests - 128 : 0, reads > 64 ? reads - 64 : 0);
}
std::uint64_t ResolveFileImport(std::string_view identifier, std::string_view library, std::string_view module) {
    // POSIX entry points have a different errno/-1 contract and are deliberately
    // not aliases of the sceKernel encoded-error ABI.
    if (library != "libkernel" || module != "libkernel") { return 0; }
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "uWyW3v98sU4") { return reinterpret_cast<std::uint64_t>(&KernelReach); }
    if (id == "1G3lF1Gg1k8") { return reinterpret_cast<std::uint64_t>(&KernelOpen); }
    if (id == "UK2Tl2DWUns") { return reinterpret_cast<std::uint64_t>(&KernelClose); }
    if (id == "Cg4srZ6TKbU") { return reinterpret_cast<std::uint64_t>(&KernelRead); }
    if (id == "4wSze92BhLI") { return reinterpret_cast<std::uint64_t>(&KernelWrite); }
    if (id == "+r3rMFwItV4") { return reinterpret_cast<std::uint64_t>(&KernelPread); }
    if (id == "oib76F-12fk") { return reinterpret_cast<std::uint64_t>(&KernelSeek); }
    if (id == "eV9wAD2riIA") { return reinterpret_cast<std::uint64_t>(&KernelStat); }
    if (id == "kBwCPsYX-m4") { return reinterpret_cast<std::uint64_t>(&KernelFstat); }
    return 0;
}

bool FileSelfTest(const std::filesystem::path& app, const std::filesystem::path& patch) {
    ConfigureNativeContent(app, patch);
    bool passed = true;
    auto check = [&](bool condition, const char* name) {
        std::printf("{\"event\":\"file_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, condition ? "true" : "false"); passed &= condition;
    };
    check(Reach("/", nullptr) == 0 && Reach("/app0", nullptr) == 0 && Reach("/app0/sce_sys/", nullptr) == 0 && Reach("/app0/no-file", nullptr) == kNoEntry,
          "root_directory_file_and_real_absence");
    const auto fd = Open("/app0/overlay.txt", 0, 0, nullptr);
    check(fd == 3, "lowest_free_descriptor_starts_at_three");
    if (fd < 0) { return false; }
    std::array<char, 9> output{}; output.back() = 73;
    check(Read(fd, output.data(), 2, 0, false, nullptr) == 2 && std::memcmp(output.data(), "pa", 2) == 0 && Seek(fd, 0, 1) == 2,
          "sequential_reads_advance_position");
    check(Read(fd, output.data(), 3, 2, true, nullptr) == 3 && std::memcmp(output.data(), "tch", 3) == 0 && Seek(fd, 0, 1) == 2 && output.back() == 73,
          "pread_is_offset_based_and_does_not_move_position_or_overwrite_tail");
    check(Seek(fd, -1, 2) == 4 && Read(fd, output.data(), 8, 0, false, nullptr) == 1 && output[0] == 'h' && Read(fd, output.data(), 1, 0, false, nullptr) == 0,
          "end_relative_seek_short_read_and_eof");
    check(Seek(fd, 1000, 0) == 1000 && Read(fd, output.data(), 8, 0, false, nullptr) == 0 && Seek(fd, -1001, 1) == kInvalid && Seek(fd, 0, 1) == 1000 &&
          Seek(fd, INT64_MAX, 0) == INT64_MAX && Seek(fd, 1, 1) == kOverflow && Seek(fd, 0, 4) == kInvalid,
          "seek_past_eof_underflow_overflow_and_invalid_whence");
    check(Read(fd, nullptr, 1, 0, false, nullptr) == kFault && Read(fd, nullptr, 0, 0, false, nullptr) == 0 &&
          Read(fd, output.data(), UINT64_MAX, 0, false, nullptr) == kInvalid && Read(fd, output.data(), 1, -1, true, nullptr) == kInvalid,
          "read_null_count_and_negative_offset_errors");
    {
        kt::memory::DirectFixture direct;
        std::memset(direct.first, 73, 16);
        const auto before = direct.Track();
        const auto n = Read(fd, direct.alias, 5, 0, true, nullptr);
        check(n == 5 && !before.Unchanged() && std::memcmp(direct.first, "patch", 5) == 0 && static_cast<char*>(direct.first)[5] == 73 && direct.Track().Unchanged(),
            "kernel_pread_updates_guarded_shared_destination_and_preserves_tail");
        check(direct.ProtectAlias(1) && Read(fd, direct.alias, 1, 0, true, nullptr) == kFault && static_cast<char*>(direct.first)[0] == 'p' && direct.ProtectAlias(3),
            "kernel_read_rejects_real_readonly_guest_destination_without_writing");
        const auto copying = direct.Track();
        const unsigned char value = 19;
        check(CopyToNativeMemory(direct.alias, &value, 1) && !copying.Unchanged() && static_cast<unsigned char*>(direct.first)[0] == value && direct.Track().Unchanged(),
            "owned_host_copy_invalidates_guarded_alias_and_releases_its_pin");
    }
    struct { FileStat value; std::uint64_t sentinel = 73; } stat{}; stat.sentinel = 73;
    check(Fstat(fd, &stat.value) == 0 && stat.value.size == 5 && stat.value.mode == 0100444 && stat.value.modified.seconds > 0 && stat.sentinel == 73 &&
          Stat("/app0/fallback.txt", &stat.value, nullptr) == 0 && stat.value.size == 8,
          "stat_120_byte_layout_actual_metadata_patch_and_base");
    check(Close(fd) == 0 && Close(fd) == kBadFd && Fstat(fd, &stat.value) == kBadFd && Read(fd, output.data(), 1, 0, false, nullptr) == kBadFd && Seek(fd, 0, 0) == kBadFd,
          "closed_and_invalid_descriptors_are_not_successful");
    bool reuse = true;
    for (unsigned i = 0; i < 1100; ++i) { const auto current = Open("/app0/overlay.txt", 0, 0, nullptr); reuse &= current == 3 && Close(current) == 0; }
    check(reuse, "closed_descriptors_reused_without_lifetime_exhaustion");
    const auto directory = Open("/app0", kDirectory, 0, nullptr);
    check(directory >= 3 && Read(directory, output.data(), 1, 0, false, nullptr) == kIsDirectory && Fstat(directory, &stat.value) == 0 &&
          stat.value.mode == 0040555 && Close(directory) == 0 && Open("/app0/overlay.txt", kDirectory, 0, nullptr) == kNotDirectory,
          "directory_handles_and_read_or_open_type_errors");
    bool readonly = true;
    for (const auto flags : {1, 2, 0x200, 0x400, 0x601}) { readonly &= Open("/app0/overlay.txt", flags, 0777, nullptr) == kReadOnly; }
    check(readonly && Open("/app0/new-file", 0x201, 0777, nullptr) == kReadOnly && !std::filesystem::exists(app / "new-file"),
          "write_create_and_truncate_rejected_without_modifying_content");
    std::memset(&stat.value, 0x73, sizeof(stat.value));
    check(Stat("/app0/missing", &stat.value, nullptr) == kNoEntry && stat.value.size == 0x7373737373737373ll && Stat("/app0", nullptr, nullptr) == kFault &&
          Reach(nullptr, nullptr) == kFault && Reach("", nullptr) == kInvalid && Reach(std::string(256, 'a').c_str(), nullptr) == kNameTooLong,
          "path_bounds_and_failed_stat_preserves_output");
    bool invalid = true;
    for (const auto path : {"/app0/../outside", "/app0/NUL", "/app0/a:stream", "/app0//overlay.txt", "/app0/CONIN$", "/app0/back\\slash"}) { invalid &= Reach(path, nullptr) == kInvalid; }
    check(invalid, "host_escape_and_alias_paths_not_opened");
    const auto parallel = Open("/app0/overlay.txt", 0, 0, nullptr);
    std::atomic<unsigned> letters{}; std::atomic<bool> correct{true};
    auto reader = [&] {
        char byte{};
        for (;;) {
            const auto n = Read(parallel, &byte, 1, 0, false, nullptr); if (!n) { break; }
            const auto index = std::string_view("patch").find(byte);
            if (n != 1 || index == std::string_view::npos) { correct = false; break; }
            letters.fetch_or(1u << static_cast<unsigned>(index));
        }
    };
    std::thread first(reader), second(reader); first.join(); second.join();
    check(correct && letters == 31 && Seek(parallel, 0, 1) == 5 && Close(parallel) == 0, "shared_descriptor_reads_are_serialized");
    std::array<int, 1021> full{}; bool capacity = true;
    for (std::size_t i = 0; i < full.size(); ++i) { full[i] = Open("/app0/overlay.txt", 0, 0, nullptr); capacity &= full[i] == static_cast<int>(i + 3); }
    capacity &= Open("/app0/overlay.txt", 0, 0, nullptr) == kTooMany && Close(full[19]) == 0;
    full[19] = Open("/app0/overlay.txt", 0, 0, nullptr); capacity &= full[19] == 22;
    for (const auto current : full) { capacity &= Close(current) == 0; }
    check(capacity, "concurrent_descriptor_limit_reports_emfile_and_recovers");
    const auto repeated = Open("/app0/overlay.txt", 0, 0, nullptr); bool bounded = repeated >= 3;
    for (unsigned i = 0; i < 80; ++i) { bounded &= Read(repeated, output.data(), 1, 0, true, nullptr) == 1 && output[0] == 'p'; }
    bounded &= Close(repeated) == 0;
    check(bounded && read_log_count > 64, "read_detail_limit_preserves_all_reads_and_counts_omissions");
    check(Time(116444735999999999ull).seconds == -1 && Time(116444735999999999ull).nanoseconds == 999999900 &&
          Time(116444736000000000ull).seconds == 0 && Time(116444736010000001ull).nanoseconds == 100,
          "filetime_epoch_negative_seconds_and_nanosecond_normalization");
    check(!ResolveFileImport("1G3lF1Gg1k8", "libScePosix", "libkernel") && !ResolveFileImport("wuCroIGjt2g", "libkernel", "libkernel") &&
          !ResolveFileImport("1G3lF1Gg1k8", "libkernel", "other"), "kernel_errors_not_bound_to_posix_or_wrong_namespace");
    PrintNativeFileDiagnostics();
    std::printf("{\"event\":\"file_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
