#include "native_fios.h"
#include "native_content.h"
#include "native_diagnostics.h"
#include "native_host_timing.h"
#include "native_memory.h"
#include "memory/direct_fixture.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

#define KT_SYSV __attribute__((sysv_abi))
namespace {
constexpr int Error(unsigned code) { return static_cast<int>(0x80820000u | code); }
constexpr int kMemory = Error(2), kNoEntry = Error(5), kAddress = Error(6), kArgument = Error(8);
constexpr int kHandle = Error(11), kBusy = Error(30), kReadOnly = Error(21), kCallbackWait = Error(36);
using Handle = std::uint32_t;
using Callback = int(KT_SYSV*)(void*, Handle, unsigned char, int);
struct Buffer { void* data{}; std::uint64_t bytes{}; };
struct Params {
    std::uint32_t header{}, profiling{}, io_threads{}, threads_per_scheduler{}, flags{}, max_chunk{};
    std::uint8_t decompressor_threads{}, internal_flags{}, reserved0{}, reserved1{};
    std::uint32_t padding{};
    std::uint64_t reserved2[2]{};
    Buffer operations, files, directories, chunks;
    void* vprintf{}; void* memcpy{}; void* profile_callback{};
    std::int32_t priorities[3]{}, affinities[3]{}, stacks[3]{}, tail_padding{};
};
struct Attr {
    std::uint64_t deadline{};
    Callback callback{};
    void* context{};
    std::uint32_t flags{}, reserved[5]{};
};
struct OpenParams { std::uint32_t flags = 1; std::uint32_t reserved[5]{}; };
struct Stat {
    std::int64_t size{}, accessed{}, modified{}, created{};
    std::uint32_t flags{}, padding{};
    std::uint64_t reserved[4]{}, mode{};
};
static_assert(sizeof(Params) == 176 && offsetof(Params, operations) == 48 && offsetof(Params, memcpy) == 120 && offsetof(Params, priorities) == 136);
static_assert(sizeof(Attr) == 48 && offsetof(Attr, flags) == 24 && sizeof(OpenParams) == 24);
static_assert(sizeof(Stat) == 80 && offsetof(Stat, mode) == 72);

bool Copy(const void* from, void* to, std::size_t bytes) {
    SIZE_T received{};
    return from && ReadProcessMemory(GetCurrentProcess(), from, to, bytes, &received) && received == bytes;
}
bool Writable(void* pointer, std::size_t bytes) {
    return NativeMemoryWritable(pointer, bytes);
}
struct InputError { int code; };
std::string Path(const char* source, unsigned maximum) {
    if (!source) { throw InputError{kAddress}; }
    std::string result;
    for (unsigned index = 0; index < maximum; ++index) {
        char ch{};
        if (!Copy(source + index, &ch, 1)) { throw InputError{kAddress}; }
        if (!ch) {
            if (result.empty() || result[0] != '/' || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, result.data(), static_cast<int>(result.size()), nullptr, 0)) {
                throw InputError{kArgument};
            }
            return result;
        }
        result += ch;
    }
    throw InputError{kNoEntry};
}
std::string Quote(std::string_view value) {
    std::string result = "\"";
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { result += '\\'; }
        if (ch < 32) { result += '?'; } else { result += static_cast<char>(ch); }
    }
    return result + '"';
}
template<typename Function> __declspec(noinline) auto Api(const void* caller, Function&& function) -> decltype(function()) {
    try { return function(); }
    catch (const InputError& error) { return static_cast<decltype(function())>(error.code); }
    catch (const kt::content::InvalidPath&) { return static_cast<decltype(function())>(kArgument); }
    catch (const kt::content::NotDirectory&) { return static_cast<decltype(function())>(Error(15)); }
    catch (const std::bad_alloc&) { return static_cast<decltype(function())>(kMemory); }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Fios provider: %s\n", error.what()); UnsupportedApi("native_fios_host_or_namespace", 0, caller);
    }
}
Attr Attributes(const Attr* supplied, const void* caller, bool fixture = false) {
    Attr result{};
    if (supplied && !Copy(supplied, &result, sizeof(result))) { throw InputError{kAddress}; }
    if ((result.deadline && result.deadline != INT64_MAX) || result.flags ||
        std::any_of(std::begin(result.reserved), std::end(result.reserved), [](auto value) { return value != 0; })) {
        UnsupportedApi("sceFios_operation_attributes", result.flags, caller);
    }
    if (result.callback && !fixture && !NativeExecutableAddress(reinterpret_cast<std::uintptr_t>(result.callback))) {
        UnsupportedApi("sceFios_callback_not_game_code", reinterpret_cast<std::uintptr_t>(result.callback), caller);
    }
    return result;
}
void SyncAttributes(const Attr* supplied, const void* caller) {
    const auto attr = Attributes(supplied, caller);
    if (attr.callback) { UnsupportedApi("sceFios_sync_callback", 0, caller); }
}
struct Pool {
    Buffer backing;
    std::size_t stride{};
    std::vector<bool> used;
    Pool() = default;
    Pool(Buffer buffer, std::size_t size) : backing(buffer), stride(size), used(static_cast<std::size_t>(buffer.bytes / size), false) {}
    std::size_t Reserve() {
        const auto it = std::find(used.begin(), used.end(), false);
        if (it == used.end()) { throw std::bad_alloc(); }
        const auto index = static_cast<std::size_t>(it - used.begin()); used[index] = true; return index;
    }
    void* At(std::size_t index) const { return static_cast<std::byte*>(backing.data) + index * stride; }
    void Release(std::size_t index) { used.at(index) = false; }
};
enum class Phase : std::uint32_t { Queued, Reading, CallbackQueued, CallbackRunning, Complete };
// These records live in the game's supplied workspace, not a dummy storage
// reservation. Host objects own Windows handles and synchronization separately.
struct FileMemory { Handle id; std::uint32_t pending; std::uint64_t size; };
struct OpMemory { Handle id; Phase phase; std::int32_t result; std::uint32_t reserved; std::uint64_t requested, actual, offset; };
struct File {
    std::unique_ptr<kt::content::ReadOnlyFile> node;
    std::size_t slot{}; FileMemory* memory{}; bool closing{};
};
struct Operation {
    std::shared_ptr<File> file;
    std::size_t slot{}; OpMemory* memory{};
    Attr attr; void* output{};
    bool deleting{};
};
thread_local Handle callback_operation{};
std::atomic<Handle> file_ids{0x46000001}, operation_ids{0x47000001};
struct Service {
    Params params;
    unsigned path_max{}, max_chunk{};
    Pool op_pool, file_pool;
    std::mutex mutex;
    std::condition_variable changed, io_ready, callback_ready;
    std::map<Handle, std::shared_ptr<File>> files;
    std::map<Handle, std::shared_ptr<Operation>> operations;
    std::vector<std::shared_ptr<Operation>> io_queue, callback_queue;
    std::vector<std::thread> workers;
    std::thread callback_worker;
    bool started{}, stopping{};
    unsigned pending_io{}, logged{};
    explicit Service(const Params& options) : params(options) {
        path_max = params.header >> 16; if (!path_max) { path_max = 1024; } path_max = std::min(path_max, 1024u);
        max_chunk = params.max_chunk ? params.max_chunk : 0x80000;
        op_pool = Pool(params.operations, (path_max + 0x107u) & ~std::size_t{7});
        file_pool = Pool(params.files, (path_max + 0x77u) & ~std::size_t{7});
        io_queue.reserve(op_pool.used.size()); callback_queue.reserve(op_pool.used.size());
    }
    ~Service() { Stop(); }
    void Stop() {
        { std::lock_guard lock(mutex); stopping = true; }
        io_ready.notify_all(); callback_ready.notify_all(); changed.notify_all();
        for (auto& thread : workers) { if (thread.joinable()) { thread.join(); } }
        callback_ready.notify_all();
        if (callback_worker.joinable()) { callback_worker.join(); }
    }
    void Start() {
        unsigned count = std::clamp(params.io_threads ? params.io_threads : 1u, 1u, 2u) *
            std::clamp(params.threads_per_scheduler ? params.threads_per_scheduler : 1u, 1u, 3u);
        for (unsigned index = 0; index < count; ++index) { workers.emplace_back([this] { ReadWorker(); }); }
        callback_worker = std::thread([this] { CallbackWorker(); });
    }
    void ReadWorker() noexcept {
        try {
            for (;;) {
                std::shared_ptr<Operation> op;
                {
                    std::unique_lock lock(mutex);
                    io_ready.wait(lock, [&] { return stopping || (started && !io_queue.empty()); });
                    if (io_queue.empty()) { if (stopping) { return; } else { continue; } }
                    op = io_queue.front(); io_queue.erase(io_queue.begin()); op->memory->phase = Phase::Reading;
                }
                int result{}; std::uint64_t actual{};
                try {
                    const auto requested = op->memory->requested, offset = op->memory->offset;
                    NativeHostOperationScope timing("sceFiosRead/io", "io", requested, nullptr);
                    auto destination = BeginNativeHostWrite(op->output, requested);
                    if (!destination) { result = kAddress; }
                    while (destination && actual < requested) {
                        const auto amount = static_cast<std::size_t>(std::min<std::uint64_t>(requested - actual, max_chunk));
                        const auto received = op->file->node->ReadAt(offset + actual,
                            {static_cast<std::byte*>(op->output) + actual, amount});
                        actual += received; if (received < amount) { result = Error(16); break; }
                    }
                } catch (const std::bad_alloc&) { result = kMemory; }
                  catch (const std::exception& error) { std::fprintf(stderr, "Fios read: %s\n", error.what()); result = Error(14); }
                {
                    std::lock_guard lock(mutex);
                    op->memory->actual = actual; op->memory->result = result;
                    --pending_io;
                    if (op->attr.callback) { op->memory->phase = Phase::CallbackQueued; callback_queue.push_back(op); }
                    else { op->memory->phase = Phase::Complete; --op->file->memory->pending; }
                    if (logged++ < 64) {
                        std::printf("{\"event\":\"native_fios_read\",\"op\":%u,\"file\":%u,\"offset\":%llu,\"requested\":%llu,\"actual\":%llu,\"result\":%d,\"callback_queued\":%s}\n",
                            op->memory->id, op->file->memory->id, op->memory->offset, op->memory->requested, actual, result, op->attr.callback ? "true" : "false");
                    }
                }
                callback_ready.notify_one(); changed.notify_all();
            }
        } catch (...) { UnsupportedApi("native_fios_io_worker", 0, nullptr); }
    }
    void CallbackWorker() noexcept {
        try {
            for (;;) {
                std::shared_ptr<Operation> op;
                {
                    std::unique_lock lock(mutex);
                    callback_ready.wait(lock, [&] { return (started && !callback_queue.empty()) || (stopping && pending_io == 0); });
                    if (callback_queue.empty()) { if (stopping && !pending_io) { return; } else { continue; } }
                    op = callback_queue.front(); callback_queue.erase(callback_queue.begin()); op->memory->phase = Phase::CallbackRunning;
                }
                callback_operation = op->memory->id;
                const auto result = op->attr.callback(op->attr.context, op->memory->id, 1, op->memory->result);
                callback_operation = 0;
                if (result) { UnsupportedApi("sceFios_callback_result", static_cast<unsigned>(result), nullptr); }
                {
                    std::lock_guard lock(mutex);
                    op->memory->phase = Phase::Complete; --op->file->memory->pending;
                }
                changed.notify_all();
            }
        } catch (...) { UnsupportedApi("native_fios_callback_worker", callback_operation, nullptr); }
    }
};
std::mutex service_mutex;
std::shared_ptr<Service> active;
std::shared_ptr<Service> Current() { std::lock_guard lock(service_mutex); return active; }
int Initialize(const Params* supplied, const void* caller) {
    return Api(caller, [&] {
        Params params{};
        if (!Copy(supplied, &params, sizeof(params))) { return kAddress; }
        if (((params.header >> 1) & 0x7fff) != sizeof(Params)) { return kArgument; }
        // The shipped FIOS initialization ABI accepts both low bits (0..3),
        // including Feveron's value 1. Neither changes this provider's read /
        // callback contract. Keep rejecting the reserved upper bits.
        if (params.profiling || params.reserved0 || params.reserved1 || params.padding || params.reserved2[0] || params.reserved2[1] ||
            params.profile_callback || (params.flags & ~3u) || params.internal_flags > 3 || params.tail_padding) { return kArgument; }
        const auto path_max = std::min(params.header >> 16 ? params.header >> 16 : 1024u, 1024u);
        const std::array buffers{params.operations, params.files, params.directories, params.chunks};
        const std::array<std::size_t, 4> strides{(path_max + 0x107u) & ~std::size_t{7}, (path_max + 0x77u) & ~std::size_t{7},
            (path_max + 0x6fu) & ~std::size_t{7}, 16};
        for (unsigned index = 0; index < buffers.size(); ++index) {
            if (reinterpret_cast<std::uintptr_t>(buffers[index].data) % 8 || buffers[index].bytes < strides[index] ||
                buffers[index].bytes / strides[index] > (index == 3 ? 65536u : 16384u) ||
                !Writable(buffers[index].data, static_cast<std::size_t>(buffers[index].bytes))) { return kArgument; }
            for (unsigned other = 0; other < index; ++other) {
                const auto a = reinterpret_cast<std::uintptr_t>(buffers[index].data), b = reinterpret_cast<std::uintptr_t>(buffers[other].data);
                if (a < b + buffers[other].bytes && b < a + buffers[index].bytes) { return kArgument; }
            }
        }
        std::lock_guard registry(service_mutex);
        if (active) {
            const auto& old = active->params;
            if (std::memcmp(&params, &old, sizeof(params)) == 0) { return 0; }
            UnsupportedApi("sceFiosInitialize_update_parameters", params.header, caller);
        }
        auto next = std::make_shared<Service>(params);
        next->Start();
        // Workers remain behind the start gate until every thread was created.
        { std::lock_guard lock(next->mutex); next->started = true; }
        active = next;
        std::printf("{\"event\":\"native_fios_initialized\",\"path_max\":%u,\"operation_capacity\":%zu,\"file_capacity\":%zu,\"io_workers\":%zu,\"callback_workers\":1,\"original_files_read_only\":true}\n",
            next->path_max, next->op_pool.used.size(), next->file_pool.used.size(), next->workers.size());
        return 0;
    });
}
int Open(const Attr* attr, Handle* output, const char* name, const OpenParams* supplied, const void* caller) {
    return Api(caller, [&] {
        SyncAttributes(attr, caller);
        if (!Writable(output, sizeof(*output))) { return kAddress; }
        auto service = Current(); if (!service) { return Error(1); }
        OpenParams options;
        if (supplied && !Copy(supplied, &options, sizeof(options))) { return kAddress; }
        if (options.flags != 1) { return kReadOnly; }
        if (std::any_of(std::begin(options.reserved), std::end(options.reserved), [](auto v) { return v != 0; })) {
            UnsupportedApi("sceFiosFHOpenSync_options", options.flags, caller);
        }
        const auto path = Path(name, service->path_max);
        auto node = OpenNativeContentFile(path, false);
        if (!node) { *output = 0; return kNoEntry; }
        auto file = std::make_shared<File>(); file->node = std::move(node);
        std::lock_guard lock(service->mutex);
        if (service->stopping) { return Error(1); }
        const auto id = file_ids.fetch_add(1);
        if (id >= 0x46ffffff) { return kMemory; }
        file->slot = service->file_pool.Reserve();
        file->memory = static_cast<FileMemory*>(service->file_pool.At(file->slot));
        try { service->files.emplace(id, file); }
        catch (...) { service->file_pool.Release(file->slot); throw; }
        ::new (file->memory) FileMemory{id, 0, file->node->Size()}; *output = id;
        std::printf("{\"event\":\"native_fios_open\",\"handle\":%u,\"path\":%s,\"size\":%llu}\n", id, Quote(path).c_str(), file->memory->size);
        return 0;
    });
}
int Close(const Attr* attr, Handle id, const void* caller) {
    return Api(caller, [&] {
        SyncAttributes(attr, caller);
        auto service = Current(); if (!service) { return Error(1); }
        std::unique_lock lock(service->mutex);
        const auto found = service->files.find(id);
        if (found == service->files.end() || found->second->closing) { return kHandle; }
        const auto file = found->second;
        if (callback_operation && file->memory->pending) { return kCallbackWait; }
        file->closing = true;
        service->changed.wait(lock, [&] { return file->memory->pending == 0; });
        service->files.erase(found); file->node.reset(); file->memory->id = 0; service->file_pool.Release(file->slot);
        return 0;
    });
}
std::int64_t Nanoseconds(std::uint64_t filetime) {
    constexpr std::uint64_t epoch = 116444736000000000ull;
    if (filetime >= epoch) {
        if (filetime - epoch > INT64_MAX / 100) { throw InputError{Error(8)}; }
        return static_cast<std::int64_t>(filetime - epoch) * 100;
    }
    if (epoch - filetime > INT64_MAX / 100) { throw InputError{Error(8)}; }
    return -static_cast<std::int64_t>(epoch - filetime) * 100;
}
int FileStat(const Attr* attr, Handle id, Stat* output, const void* caller) {
    return Api(caller, [&] {
        SyncAttributes(attr, caller);
        if (!Writable(output, sizeof(*output))) { return kAddress; }
        auto service = Current(); if (!service) { return Error(1); }
        std::lock_guard lock(service->mutex);
        const auto found = service->files.find(id);
        if (found == service->files.end() || found->second->closing) { return kHandle; }
        const auto& info = found->second->node->Info();
        if (info.size > INT64_MAX) { return kArgument; }
        Stat stat{}; stat.size = static_cast<std::int64_t>(info.size);
        stat.accessed = Nanoseconds(info.access_time); stat.modified = Nanoseconds(info.modification_time); stat.created = Nanoseconds(info.creation_time);
        stat.flags = info.directory ? 3 : 2; stat.mode = info.directory ? 0040555 : 0100444; *output = stat;
        return 0;
    });
}
__declspec(noinline) bool Exists(const Attr* attr, const char* name, const void* caller) {
    try {
        SyncAttributes(attr, caller);
        const auto service = Current(); const auto path = Path(name, service ? service->path_max : 1024);
        const auto node = OpenNativeContentFile(path, true); const bool exists = node && !node->Info().directory;
        std::printf("{\"event\":\"native_fios_exists\",\"path\":%s,\"exists\":%s}\n", Quote(path).c_str(), exists ? "true" : "false"); return exists;
    } catch (const InputError&) { return false; }
      catch (const kt::content::InvalidPath&) { return false; }
      catch (const kt::content::NotDirectory&) { return false; }
      catch (...) { UnsupportedApi("sceFiosFileExistsSync_namespace", 0, caller); }
}
__declspec(noinline) Handle Pread(const Attr* supplied, Handle id, void* data, std::int64_t length, std::int64_t offset, const void* caller, bool fixture = false) {
    try {
        const auto attr = Attributes(supplied, caller, fixture);
        auto service = Current(); if (!service) { return 0; }
        if (length < 0 || offset < 0 || length > INT64_MAX - offset || !Writable(data, static_cast<std::size_t>(std::max<std::int64_t>(length, 0)))) { return 0; }
        auto op = std::make_shared<Operation>(); op->attr = attr; op->output = data;
        std::unique_lock lock(service->mutex);
        const auto file = service->files.find(id);
        if (service->stopping || file == service->files.end() || file->second->closing) { return 0; }
        const auto token = operation_ids.fetch_add(1);
        if (token >= 0x47ffffff) { return 0; }
        op->slot = service->op_pool.Reserve(); op->file = file->second;
        op->memory = static_cast<OpMemory*>(service->op_pool.At(op->slot));
        try { service->operations.emplace(token, op); }
        catch (...) { service->op_pool.Release(op->slot); throw; }
        ::new (op->memory) OpMemory{token, Phase::Queued, 0, 0, static_cast<std::uint64_t>(length), 0, static_cast<std::uint64_t>(offset)};
        ++op->file->memory->pending; ++service->pending_io; service->io_queue.push_back(op);
        lock.unlock(); service->io_ready.notify_one(); return token;
    } catch (const InputError&) { return 0; }
      catch (const std::bad_alloc&) { return 0; }
      catch (...) { UnsupportedApi("sceFiosFHPread_host", id, caller); }
}
bool IsDone(Handle id) {
    const auto service = Current(); if (!service) { return true; }
    std::lock_guard lock(service->mutex);
    const auto found = service->operations.find(id);
    return found == service->operations.end() || found->second->memory->phase == Phase::Complete;
}
int Delete(Handle id) {
    const auto service = Current(); if (!service) { return kHandle; }
    std::unique_lock lock(service->mutex);
    const auto found = service->operations.find(id);
    if (found == service->operations.end() || found->second->deleting) { return kHandle; }
    const auto op = found->second;
    if (callback_operation && op->memory->phase != Phase::Complete) { return kCallbackWait; }
    op->deleting = true;
    service->changed.notify_all();
    service->changed.wait(lock, [&] { return op->memory->phase == Phase::Complete; });
    // Original OpDelete returns pool-release status. Read result/actual count
    // belong to the callback/operation, not this destruction return value.
    service->operations.erase(found); op->memory->id = 0; service->op_pool.Release(op->slot); return 0;
}
int Terminate() {
    std::shared_ptr<Service> service;
    {
        std::lock_guard registry(service_mutex);
        if (!active) { return Error(1); }
        std::lock_guard lock(active->mutex);
        if (!active->files.empty() || !active->operations.empty()) { return kBusy; }
        service = std::move(active);
    }
    service->Stop(); return 0;
}
int KT_SYSV FiosInitialize(const Params* supplied) {
    const auto caller=__builtin_return_address(0);
    const auto result=Initialize(supplied,caller);
    if (result) {
        Params params{};
        if (Copy(supplied,&params,sizeof(params))) {
            std::printf("{\"event\":\"native_fios_initialize_rejected\",\"result\":%d,\"header\":%u,\"parameter_bytes\":%u,"
                "\"profiling\":%u,\"flags\":%u,\"internal_flags\":%u,\"operation_bytes\":%llu,\"file_bytes\":%llu,"
                "\"directory_bytes\":%llu,\"chunk_bytes\":%llu}\n",result,params.header,(params.header>>1)&0x7fff,
                params.profiling,params.flags,params.internal_flags,params.operations.bytes,params.files.bytes,
                params.directories.bytes,params.chunks.bytes);
        }
        TraceNativeApi("sceFiosInitialize_rejected",static_cast<unsigned>(result),caller);
    }
    return result;
}
int KT_SYSV FiosOpen(const Attr* attr, Handle* output, const char* path, const OpenParams* params) { return Open(attr, output, path, params, __builtin_return_address(0)); }
int KT_SYSV FiosClose(const Attr* attr, Handle file) { return Close(attr, file, __builtin_return_address(0)); }
int KT_SYSV FiosStat(const Attr* attr, Handle file, Stat* stat) { return FileStat(attr, file, stat, __builtin_return_address(0)); }
bool KT_SYSV FiosExists(const Attr* attr, const char* path) { return Exists(attr, path, __builtin_return_address(0)); }
Handle KT_SYSV FiosPread(const Attr* attr, Handle file, void* data, std::int64_t bytes, std::int64_t offset) { return Pread(attr, file, data, bytes, offset, __builtin_return_address(0)); }
bool KT_SYSV FiosIsDone(Handle op) { return IsDone(op); }
int KT_SYSV FiosDelete(Handle op) { return Api(__builtin_return_address(0), [&] { return Delete(op); }); }
}

std::uint64_t ResolveFiosImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceFios2" || module != "libSceFios2") { return 0; }
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "wAKZ-det+yo") { return reinterpret_cast<std::uint64_t>(&FiosInitialize); }
    if (id == "b44anV2D7K0") { return reinterpret_cast<std::uint64_t>(&FiosOpen); }
    if (id == "AOujSGqU+ms") { return reinterpret_cast<std::uint64_t>(&FiosClose); }
    if (id == "xP45eIntEis") { return reinterpret_cast<std::uint64_t>(&FiosStat); }
    if (id == "NwOHMRM2Ppw") { return reinterpret_cast<std::uint64_t>(&FiosExists); }
    if (id == "rR8wq7YFRZs") { return reinterpret_cast<std::uint64_t>(&FiosPread); }
    if (id == "bfgo2Otmqz0") { return reinterpret_cast<std::uint64_t>(&FiosIsDone); }
    if (id == "5cyEcilO-J0") { return reinterpret_cast<std::uint64_t>(&FiosDelete); }
    return 0;
}

namespace {
struct FixtureCallbackState {
    std::mutex mutex; std::condition_variable changed;
    Handle file{}, operation{};
    bool entered{}, release{}, reentered{};
    int status{};
};
int KT_SYSV FixtureCallback(void* context, Handle op, unsigned char event, int status) {
    auto& state = *static_cast<FixtureCallbackState*>(context);
    const bool reentered = event == 1 && Delete(op) == kCallbackWait && Close(nullptr, state.file, nullptr) == kCallbackWait;
    std::unique_lock lock(state.mutex);
    state.operation = op; state.status = status; state.reentered = reentered; state.entered = true;
    state.changed.notify_all(); state.changed.wait(lock, [&] { return state.release; }); return 0;
}
}
bool FiosSelfTest(const std::filesystem::path& app, const std::filesystem::path& patch) {
    using namespace std::chrono_literals;
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value; std::printf("{\"event\":\"fios_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    ConfigureNativeContent(app, patch);
    std::array<std::uint64_t, 160> ops; ops.fill(73);
    std::array<std::uint64_t, 44> files; files.fill(73);
    std::array<std::uint64_t, 21> dirs; dirs.fill(73);
    std::array<std::uint64_t, 8> chunks; chunks.fill(73);
    Params params{}; params.header = (64u << 16) | (sizeof(Params) << 1); params.io_threads = 2; params.threads_per_scheduler = 1; params.max_chunk = 3;
    params.operations = {ops.data(), sizeof(ops)}; params.files = {files.data(), sizeof(files)};
    params.directories = {dirs.data(), sizeof(dirs)}; params.chunks = {chunks.data(), sizeof(chunks)};
    auto invalid = params; invalid.header -= 2;
    check("invalid_size_preserves_storage_and_does_not_start_workers", Initialize(&invalid, nullptr) == kArgument && !Current() && ops[0] == 73);
    invalid = params; invalid.files = invalid.operations;
    check("overlapping_workspaces_are_rejected", Initialize(&invalid, nullptr) == kArgument && !Current());
    invalid = params; invalid.flags = 4;
    check("reserved_initialization_flag_preserves_storage_and_starts_no_workers", Initialize(&invalid, nullptr) == kArgument && !Current() && ops[0] == 73);
    const auto original_params = params;
    check("initialization_starts_bounded_workers_without_modifying_params", Initialize(&params, nullptr) == 0 && Current()->workers.size() == 2 &&
        Current()->op_pool.used.size() == 4 && Current()->file_pool.used.size() == 2 && std::memcmp(&params, &original_params, sizeof(params)) == 0);
    check("identical_reinitialization_does_not_replace_live_service", Initialize(&params, nullptr) == 0 && Current()->workers.size() == 2);
    check("exists_resolves_patch_fallback_and_real_absence", Exists(nullptr, "/app0/overlay.txt", nullptr) && Exists(nullptr, "/app0/fallback.txt", nullptr) &&
        !Exists(nullptr, "/app0", nullptr) && !Exists(nullptr, "/app0/missing", nullptr) && !Exists(nullptr, "/app0/../escape", nullptr));
    Handle file = 73;
    OpenParams write{}; write.flags = 2;
    check("write_request_preserves_output_and_original_files", Open(nullptr, &file, "/app0/overlay.txt", &write, nullptr) == kReadOnly && file == 73);
    check("real_missing_file_has_no_success_handle", Open(nullptr, &file, "/app0/missing", nullptr, nullptr) == kNoEntry && file == 0);
    check("open_uses_provided_file_workspace", Open(nullptr, &file, "/app0/overlay.txt", nullptr, nullptr) == 0 && file && files[0] != 73);
    struct { Stat stat; std::uint64_t tail = 73; } metadata{};
    check("stat_has_verified_80_byte_layout_and_patch_size", FileStat(nullptr, file, &metadata.stat, nullptr) == 0 && metadata.stat.size == 5 &&
        metadata.stat.mode == 0100444 && metadata.stat.flags == 2 && metadata.stat.modified > 0 && metadata.tail == 73);
    {
        kt::memory::DirectFixture direct;
        std::memset(direct.first, 73, 16);
        const auto before = direct.Track();
        const auto reading = Pread(nullptr, file, direct.alias, 5, 0, nullptr);
        check("async_readfile_updates_guarded_physical_alias_before_completion", reading && Delete(reading) == 0 &&
            !before.Unchanged() && std::memcmp(direct.first, "patch", 5) == 0 && static_cast<char*>(direct.first)[5] == 73 && direct.Track().Unchanged());
        const auto again = direct.Track();
        const auto repeated = Pread(nullptr, file, direct.alias, 3, 2, nullptr);
        check("async_destination_can_be_rearmed_and_reused_without_stale_texture_proof", repeated && Delete(repeated) == 0 && !again.Unchanged() &&
            std::memcmp(direct.first, "tch", 3) == 0 && direct.Track().Unchanged());
        check("async_request_does_not_publish_real_guest_readonly_destination", direct.ProtectAlias(1) &&
            !Pread(nullptr, file, direct.alias, 1, 0, nullptr) && static_cast<char*>(direct.first)[0] == 't' && direct.ProtectAlias(3));
    }
    FixtureCallbackState callback; callback.file = file;
    Attr attr{}; attr.callback = FixtureCallback; attr.context = &callback;
    std::array<char, 9> data; data.fill(73);
    const auto op = Pread(&attr, file, data.data(), 5, 0, nullptr, true);
    bool entered{};
    { std::unique_lock lock(callback.mutex); entered = callback.changed.wait_for(lock, 2s, [&] { return callback.entered; }); }
    check("async_read_copies_data_before_reentrant_completion_callback", op && entered && callback.operation == op && callback.status == 0 && callback.reentered &&
        std::memcmp(data.data(), "patch", 5) == 0 && data[5] == 73);
    check("completion_is_not_visible_until_callback_returns", !IsDone(op) && Terminate() == kBusy);
    std::atomic<int> deleted{73}; std::thread deleter([&] { deleted = Delete(op); });
    bool waiting{};
    const auto service = Current();
    { std::unique_lock lock(service->mutex); waiting = service->changed.wait_for(lock, 2s, [&] { return service->operations.at(op)->deleting; }); }
    check("delete_waits_for_actual_callback_completion", waiting && deleted == 73);
    { std::lock_guard lock(callback.mutex); callback.release = true; } callback.changed.notify_all(); deleter.join();
    check("completed_operation_is_retired_once", deleted == 0 && IsDone(op) && Delete(op) == kHandle && static_cast<Handle>(ops[0]) == 0);
    data.fill(73);
    const auto offset_op = Pread(nullptr, file, data.data(), 3, 2, nullptr);
    check("positional_read_is_real_and_independent_of_previous_read", offset_op && Delete(offset_op) == 0 && std::memcmp(data.data(), "tch", 3) == 0 && data[3] == 73);
    const auto eof_op = Pread(nullptr, file, data.data(), 6, 3, nullptr);
    {
        std::unique_lock lock(service->mutex);
        service->changed.wait_for(lock, 2s, [&] { return service->operations.at(eof_op)->memory->phase == Phase::Complete; });
        const auto memory = service->operations.at(eof_op)->memory;
        check("short_read_records_eof_and_actual_byte_count", memory->result == Error(16) && memory->actual == 2 && std::memcmp(data.data(), "ch", 2) == 0);
    }
    check("eof_operation_can_be_deleted_without_faking_delete_error", Delete(eof_op) == 0);
    check("invalid_read_arguments_do_not_publish_operation", !Pread(nullptr, file, nullptr, 1, 0, nullptr) && !Pread(nullptr, file, data.data(), -1, 0, nullptr) &&
        !Pread(nullptr, file, data.data(), 1, -1, nullptr) && !Pread(nullptr, 0, data.data(), 1, 0, nullptr));
    std::array<Handle, 4> full{}; bool bounded = true;
    for (auto& token : full) { token = Pread(nullptr, file, data.data(), 0, 0, nullptr); bounded &= token != 0; }
    bounded &= !Pread(nullptr, file, data.data(), 0, 0, nullptr);
    for (const auto token : full) { bounded &= Delete(token) == 0; }
    check("operation_capacity_recovered_only_after_delete", bounded);
    check("close_retires_handle_and_releases_windows_file", Close(nullptr, file, nullptr) == 0 && Close(nullptr, file, nullptr) == kHandle &&
        FileStat(nullptr, file, &metadata.stat, nullptr) == kHandle);
    check("terminate_joins_workers_after_empty_graph", Terminate() == 0 && !Current());
    check("restart_never_reuses_stale_file_identity", Initialize(&params, nullptr) == 0 &&
        Open(nullptr, &callback.file, "/app0/fallback.txt", nullptr, nullptr) == 0 && callback.file != file && Close(nullptr, file, nullptr) == kHandle &&
        Close(nullptr, callback.file, nullptr) == 0 && Terminate() == 0);
    for (const unsigned flags : {1u, 2u, 3u}) {
        auto options = params; options.flags = flags;
        const auto initialized = Initialize(&options, nullptr);
        Handle opened{};
        bool valid = initialized == 0 && Current() && Current()->params.flags == flags;
        if (initialized == 0) {
            valid &= Open(nullptr, &opened, "/app0/overlay.txt", nullptr, nullptr) == 0;
            data.fill(73);
            const auto reading = opened ? Pread(nullptr, opened, data.data(), 5, 0, nullptr) : 0;
            valid &= reading && Delete(reading) == 0 && std::memcmp(data.data(), "patch", 5) == 0 && data[5] == 73;
            if (opened) { valid &= Close(nullptr, opened, nullptr) == 0; }
            valid &= Terminate() == 0 && !Current();
        }
        const auto name = "initialization_flags_" + std::to_string(flags) + "_preserve_real_async_read_and_shutdown";
        check(name.c_str(), valid);
    }
    check("exact_library_and_module_identity_are_required", !ResolveFiosImport("wAKZ-det+yo", "wrong", "libSceFios2") &&
        !ResolveFiosImport("wAKZ-det+yo", "libSceFios2", "wrong"));
    std::printf("{\"event\":\"fios_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
