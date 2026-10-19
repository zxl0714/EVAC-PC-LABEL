#include <windows.h>
#include <bcrypt.h>
#include "guest_heap.h"
#include "native_debug.h"
#include "native_sync.h"
#include "native_semaphore.h"
#include "native_threads.h"
#include "native_dialogs.h"
#include "native_diagnostics.h"
#include "native_time.h"
#include "native_host_timing.h"
#include "native_async_log.h"
#include "native_rtc.h"
#include "native_libc_time.h"
#include "native_user.h"
#include "native_screenshot.h"
#include "native_storage.h"
#include "native_memory.h"
#include "native_gpu.h"
#include "native_resident_gpu.h"
#include "native_trophy.h"
#include "memory/write_tracker.h"
#include "memory/tracked_texture.h"
#include "native_mspace.h"
#include "native_video.h"
#include "native_libc.h"
#include "native_text.h"
#include "native_format.h"
#include "native_audio.h"
#include "native_content.h"
#include "native_files.h"
#include "native_fios.h"
#include "native_event_flag.h"
#include "native_gnm.h"
#include "graphics/shader_compiler.h"
#include "native_math.h"
#include "graphics/pm4.h"
#include "native_ngs2.h"
#include "native_avplayer.h"
#include "audio/pcm_voice.h"
#include "native_profile.h"
#include "native_cxx.h"
#include "native_equeue.h"
#include "native_display_clock.h"
#include "native_pad.h"
#include "input/sdl_gamepad.h"
#include "native_np.h"
#include "community/client.h"
#include "community/replay.h"
#include "community/round_journal.h"
#include "native_np_score.h"
#include "native_community_round_audit.h"
#include "native_community_rounds.h"
#include "graphics/display_layout.h"
#include "collection_config.h"
#include "collection_launch.h"
#include "loader/elf_prepare.h"
#include "loader/prepared_cache.h"
#include "content/resource_prepare.h"

#include <array>
#include <algorithm>
#include <bit>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#define KT_SYSV __attribute__((sysv_abi))

namespace {

constexpr std::uint64_t kPage = 4096;
constexpr std::uint64_t kMaximumImage = 512ull * 1024 * 1024;
constexpr std::uint64_t kThunkBytes = 128;
static_assert(sizeof(std::atomic<std::uint64_t>) == 8 && std::atomic<std::uint64_t>::is_always_lock_free);
const unsigned char kNoThrowTag = 0;

std::uint64_t Align(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

std::string Quoted(const std::string& value) {
    std::string result = "\"";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 32) {
            char escaped[7];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            result += escaped;
        } else {
            result += static_cast<char>(c);
        }
    }
    return result + '"';
}

void LoadKeyboardConfiguration(const wchar_t* explicit_path = nullptr) {
    std::filesystem::path path;
    if (explicit_path) {
        if (!*explicit_path) { throw std::runtime_error("Input config path is empty"); }
        path = std::filesystem::absolute(explicit_path);
    } else {
        std::array<wchar_t, 32768> executable{};
        const auto count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!count || count >= executable.size()) { throw std::runtime_error("Cannot find executable directory for input.ini"); }
        path = std::filesystem::path(executable.data()).parent_path() / L"input.ini";
    }
    const auto utf8_path = path.u8string();
    std::printf("{\"event\":\"native_pad_keyboard_config_path\",\"path\":%s,\"explicit\":%s}\n",
        Quoted(std::string(utf8_path.begin(), utf8_path.end())).c_str(), explicit_path ? "true" : "false");
    if (!explicit_path && !std::filesystem::exists(path)) {
        std::puts("{\"event\":\"native_pad_keyboard_config\",\"loaded\":false,\"using_defaults\":true}");
        return;
    }
    ConfigureNativePadKeyboard(path);
}

class Reader {
public:
    explicit Reader(const std::filesystem::path& path) : stream_(path, std::ios::binary) {
        if (!stream_ || std::filesystem::file_size(path) > kMaximumImage) {
            throw std::runtime_error("Missing or oversized native probe input");
        }
    }

    void Read(void* target, std::size_t size) {
        if (size > kMaximumImage || !stream_.read(static_cast<char*>(target), static_cast<std::streamsize>(size))) {
            throw std::runtime_error("Truncated native probe input");
        }
    }

    template <typename T>
    T Value() {
        T result{};
        Read(&result, sizeof(result));
        return result;
    }

    std::string String() {
        const auto size = Value<std::uint32_t>();
        if (size > 4096) {
            throw std::runtime_error("Oversized import name");
        }
        std::string result(size, '\0');
        Read(result.data(), size);
        if (result.find('\0') != std::string::npos) {
            throw std::runtime_error("Embedded NUL in import name");
        }
        return result;
    }

    void End() {
        if (stream_.peek() != std::char_traits<char>::eof()) {
            throw std::runtime_error("Trailing native probe input data");
        }
    }

private:
    std::ifstream stream_;
};

struct Header {
    std::array<char, 8> magic;
    std::uint64_t image_size, entry, segments, relocations, imports, procparam, capabilities;
    std::array<std::uint8_t, 32> source_sha256;
};
static_assert(sizeof(Header) == 96);

struct Segment {
    std::uint64_t address, memory_size, flags, file_size;
};

struct Relocation {
    std::uint64_t address, kind, value;
    std::int64_t addend;
};

struct Import {
    std::string nid, name, library, module;
    std::uint32_t type;
    std::uint64_t address{};
    bool pending = false;
};

struct ModuleMetadata { std::uint64_t init{}, fini{}, flags{}, exports{}; };
struct Export { Import symbol; std::uint32_t binding{}; std::uint64_t offset{}, size{}; };

struct ExitCallback {
    void(KT_SYSV* plain)(){};
    void(KT_SYSV* object)(void*){};
    void* argument{};
    void* dso{};
};

struct Runtime {
    GuestHeap heap;
    unsigned char* image{};
    unsigned char* thunks{};
    unsigned char* guards{};
    std::atomic<std::uint64_t>* binding_cells{};
    std::vector<Relocation> import_relocations;
    std::uint64_t* stack_guard{};
    Header header{};
    std::vector<Segment> segments;
    std::vector<Import> imports;
    std::mutex exit_mutex;
    std::recursive_mutex exit_dispatch_mutex;
    std::vector<ExitCallback> exit_callbacks;
    bool entered{};
    bool is_module{}, started{}, owns_guard = true;
    ModuleMetadata module_info;
    std::vector<Export> exports;
    std::string image_name = "eboot.bin";
    int module_handle{}, start_result{};
    std::vector<std::unique_ptr<Runtime>> modules;
    ~Runtime() {
        if (image) { VirtualFree(image, 0, MEM_RELEASE); }
        if (thunks) { VirtualFree(thunks, 0, MEM_RELEASE); }
        if (guards) { VirtualFree(guards, 0, MEM_RELEASE); }
        if (stack_guard && owns_guard) { VirtualFree(stack_guard, 0, MEM_RELEASE); }
    }
};

Runtime* active_runtime{};
std::array<std::atomic<Runtime*>, 16> module_images{};
std::recursive_mutex module_gate;
std::filesystem::path module_directory;
Runtime* ImageAt(std::uintptr_t address) {
    const auto contains = [&](Runtime* runtime) {
        const auto base = runtime ? reinterpret_cast<std::uintptr_t>(runtime->image) : 0;
        return runtime && address >= base && address - base < runtime->header.image_size;
    };
    if (contains(active_runtime)) { return active_runtime; }
    for (auto& entry : module_images) { auto* runtime = entry.load(std::memory_order_acquire); if (contains(runtime)) { return runtime; } }
    return nullptr;
}
int KT_SYSV NativeLoadStartModule(const char*, std::uint64_t, const void*, unsigned, const void*, int*);
int KT_SYSV NativeDlsym(int, const char*, void**);
thread_local int native_errno{};
std::atomic<bool> terminal_log_test{}, terminal_log_announced{};
std::atomic_flag requested_close_owner = ATOMIC_FLAG_INIT;
void HoldTerminalOutput() {
    // The worker uses stdout too. Drain before owning its CRT lock, never
    // wait for the worker while holding that lock.
    (void)StopNativeAsyncLog();
    // Controlled diagnostic exits own the CRT stream through process death.
    // This waits for any current printf to finish and prevents another thread
    // from starting a partial line after the terminal record has been written.
    // Do not use this in the lock-independent unexpected-fault handler.
    _lock_file(stdout);
    PrintNativeAsyncLogDiagnostics();
}
DWORD WINAPI TerminalTestWriter(void*) {
    while (!terminal_log_announced.load(std::memory_order_acquire)) { Sleep(0); }
    _lock_file(stdout);
    constexpr char prefix[] = "{\"event\":\"late_writer";
    _fwrite_nolock(prefix, 1, sizeof(prefix) - 1, stdout);
    Sleep(200);
    constexpr char suffix[] = "\"}\n";
    _fwrite_nolock(suffix, 1, sizeof(suffix) - 1, stdout);
    _unlock_file(stdout); return 0;
}
void KT_SYSV ProgramExit();
bool Contains(const Runtime& runtime, std::uint64_t address, std::uint64_t bytes,
              bool executable = false);

void TraceAllocation(const char* name, std::size_t size, void* pointer) {
    static std::atomic<std::uint64_t> calls{};
    const auto call = ++calls;
    if (call <= 32 || call % 1024 == 0 || !pointer) {
        std::printf("{\"event\":\"native_allocation\",\"name\":\"%s\",\"size\":%zu,"
                    "\"pointer\":\"%p\",\"call\":%llu}\n", name, size, pointer, call);
    }
}

void* KT_SYSV NativeNew(std::size_t size) {
    NativeHostOperationScope timing("operator new", "allocation", size, __builtin_return_address(0), true, NativeHostAllocationKind::New);
    void* pointer = active_runtime->heap.Allocate(size);
    TraceAllocation("operator new", size, pointer);
    if (!pointer) {
        // The Windows and original game's C++ exception ABIs are incompatible.
        // Stop on this unsupported path instead of returning nullptr or throwing
        // a host std::bad_alloc through an ELF stack.
        std::puts("{\"event\":\"unimplemented_cpp_exception\",\"reason\":\"allocation_failure\"}");
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 20);
        ExitProcess(20);
    }
    return pointer;
}

void* KT_SYSV NativeNewNoThrow(std::size_t size, const void*) {
    NativeHostOperationScope timing("operator new(nothrow)", "allocation", size, __builtin_return_address(0), true, NativeHostAllocationKind::NothrowNew);
    void* pointer = active_runtime->heap.Allocate(size);
    TraceAllocation("operator new(nothrow)", size, pointer);
    return pointer;
}

void* KT_SYSV NativeMalloc(std::size_t size) {
    NativeHostOperationScope timing("malloc", "allocation", size, __builtin_return_address(0), true, NativeHostAllocationKind::Malloc);
    void* pointer = active_runtime->heap.Allocate(size);
    TraceAllocation("malloc", size, pointer);
    if (!pointer) { native_errno = 12; } // Orbis ENOMEM, not a Windows errno
    return pointer;
}
void* KT_SYSV NativeCalloc(std::size_t count,std::size_t size) {
    auto* pointer=active_runtime->heap.AllocateZeroed(count,size);
    TraceAllocation("calloc",size && count>(std::numeric_limits<std::size_t>::max)()/size?SIZE_MAX:count*size,pointer);
    if (!pointer) native_errno=12;
    return pointer;
}

void* KT_SYSV NativeMemalign(std::size_t alignment, std::size_t size) {
    NativeHostOperationScope timing("memalign", "allocation", size, __builtin_return_address(0), true, NativeHostAllocationKind::Memalign);
    // The original libc routes this to its mspace memalign: small values use
    // the default allocator; larger non-powers round upward (not POSIX rules).
    if (alignment > static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)())) { native_errno = 12; return nullptr; }
    alignment = alignment <= 32 ? 32 : std::bit_ceil(alignment);
    auto* pointer = active_runtime->heap.AllocateAligned(size, alignment);
    TraceAllocation("memalign", size, pointer);
    if (!pointer) { native_errno = 12; }
    return pointer;
}

int* KT_SYSV NativeError() { return &native_errno; }

void KT_SYSV NativeDelete(void* pointer) {
    NativeHostOperationScope timing("free/operator delete", "allocation", 0, __builtin_return_address(0), true, NativeHostAllocationKind::FreeDelete);
    if (!active_runtime->heap.Release(pointer)) {
        std::printf("{\"event\":\"invalid_heap_pointer\",\"pointer\":\"%p\"}\n", pointer);
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 24);
        ExitProcess(24);
    }
}

void KT_SYSV VerifiedInitEnv() {
    std::puts("{\"event\":\"native_api\",\"name\":\"_init_env\",\"contract\":\"verified_ret\"}");
}

bool AcceptCallback(std::uintptr_t address, bool host_exit = false) {
    const auto* runtime = ImageAt(address);
    const auto base = runtime ? reinterpret_cast<std::uintptr_t>(runtime->image) : 0;
    return address && ((host_exit && address == reinterpret_cast<std::uintptr_t>(&ProgramExit)) ||
           (runtime && Contains(*runtime, address - base, 1, true)));
}

int RegisterExit(ExitCallback callback, const char* name) {
    try {
        std::lock_guard lock(active_runtime->exit_mutex);
        if (active_runtime->exit_callbacks.size() >= 4096) {
            return -1;
        }
        active_runtime->exit_callbacks.push_back(callback);
        std::printf("{\"event\":\"native_api\",\"name\":\"%s\",\"registered\":%zu}\n",
                    name, active_runtime->exit_callbacks.size());
        return 0;
    } catch (...) {
        return -1;
    }
}

int KT_SYSV NativeAtexit(void(KT_SYSV* callback)()) {
    return AcceptCallback(reinterpret_cast<std::uintptr_t>(callback), true) ?
           RegisterExit({callback, nullptr, nullptr, nullptr}, "atexit") : -1;
}

int KT_SYSV NativeCxaAtexit(void(KT_SYSV* callback)(void*), void* argument, void* dso) {
    return AcceptCallback(reinterpret_cast<std::uintptr_t>(callback)) ?
           RegisterExit({nullptr, callback, argument, dso}, "__cxa_atexit") : -1;
}

void DispatchExit(void* dso) {
    // Remove before calling, so recursive finalization and repeated unload do
    // not execute an entry twice. Serial dispatch preserves registration order
    // while the queue mutex is not held across a guest callback.
    std::lock_guard dispatch(active_runtime->exit_dispatch_mutex);
    for (;;) {
        ExitCallback callback{};
        bool found = false;
        {
            std::lock_guard lock(active_runtime->exit_mutex);
            auto& callbacks = active_runtime->exit_callbacks;
            for (auto entry = callbacks.end(); entry != callbacks.begin();) {
                --entry;
                if (!dso || entry->dso == dso) {
                    callback = *entry;
                    callbacks.erase(entry);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            break;
        }
        if (callback.plain) {
            std::printf("{\"event\":\"atexit_callback\",\"address\":\"%p\"}\n",
                        reinterpret_cast<void*>(callback.plain));
            callback.plain();
        } else {
            std::printf("{\"event\":\"cxa_atexit_callback\",\"address\":\"%p\",\"argument\":\"%p\","
                        "\"dso\":\"%p\"}\n", reinterpret_cast<void*>(callback.object),
                        callback.argument, callback.dso);
            callback.object(callback.argument);
        }
    }
}

void KT_SYSV NativeCxaFinalize(void* dso) {
    DispatchExit(dso);
}

[[noreturn]] KT_SYSV void NativeExit(int status) {
    DispatchExit(nullptr);
    (void)StopNativeAsyncLog();
    PrintNativeAsyncLogDiagnostics();
    PrintNativeFileDiagnostics();
    PrintNativeHostTimingDiagnostics();
    std::fflush(nullptr);
    std::printf("{\"event\":\"native_exit\",\"status\":%d}\n", status);
    ExitProcess(static_cast<UINT>(status));
}

[[noreturn]] KT_SYSV void NativeImmediateExit(int status) {
    (void)StopNativeAsyncLog();
    PrintNativeAsyncLogDiagnostics();
    PrintNativeFileDiagnostics();
    PrintNativeHostTimingDiagnostics();
    std::printf("{\"event\":\"native_immediate_exit\",\"status\":%d}\n", status);
    ExitProcess(static_cast<UINT>(status));
}

[[noreturn]] KT_SYSV void NativeStackCheckFailure() {
    std::puts("{\"event\":\"stack_check_failure\"}");
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 23);
    ExitProcess(23);
}

[[noreturn]] KT_SYSV void MissingImport(const Import* import, std::uintptr_t caller,
                                       const std::uintptr_t* arguments) {
    HoldTerminalOutput();
    const auto* image = ImageAt(caller);
    if (!image) { image = active_runtime; }
    const auto offset = caller - reinterpret_cast<std::uintptr_t>(image->image);
    PrintNativeFileDiagnostics();
    PrintNativeHostTimingDiagnostics();
    std::printf("{\"event\":\"unimplemented_import\",\"nid\":%s,\"name\":%s,"
                "\"library\":%s,\"module\":%s,\"caller_offset\":\"0x%llx\","
                "\"argument0\":\"0x%llx\",\"arguments\":[\"0x%llx\",\"0x%llx\",\"0x%llx\",\"0x%llx\",\"0x%llx\",\"0x%llx\"],"
                "\"game_entry_reached\":true,\"caller_image\":%s}\n",
                Quoted(import->nid).c_str(), Quoted(import->name).c_str(),
                Quoted(import->library).c_str(), Quoted(import->module).c_str(),
                static_cast<unsigned long long>(offset), static_cast<unsigned long long>(arguments[0]),
                static_cast<unsigned long long>(arguments[0]), static_cast<unsigned long long>(arguments[1]),
                static_cast<unsigned long long>(arguments[2]), static_cast<unsigned long long>(arguments[3]),
                static_cast<unsigned long long>(arguments[4]), static_cast<unsigned long long>(arguments[5]), Quoted(image->image_name).c_str());
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 20);
    ExitProcess(20);
}

// The guest may have faulted inside a CRT call with its locks still held. Keep
// this last-resort path independent of printf, allocation, and C++ unwinding.
class FaultLine {
public:
    void Text(const char* text) {
        while (*text && used_ < sizeof(buffer_)) {
            buffer_[used_++] = *text++;
        }
    }
    void Hex(std::uint64_t value) {
        Text("\"0x");
        bool significant = false;
        for (int shift = 60; shift >= 0; shift -= 4) {
            const auto digit = static_cast<unsigned>((value >> shift) & 15);
            if (digit || significant || !shift) {
                significant = true;
                const char text[]{"0123456789abcdef"[digit], 0};
                Text(text);
            }
        }
        Text("\"");
    }
    void Quote(const std::string& text) {
        Text("\"");
        std::size_t count = 0;
        for (const unsigned char c : text) {
            if (++count > 256) {
                Text("...");
                break;
            }
            if (c == '"' || c == '\\') {
                Text("\\");
            } else if (c < 32 || c >= 127) {
                Text("?");
                continue;
            }
            const char character[]{static_cast<char>(c), 0};
            Text(character);
        }
        Text("\"");
    }
    void Flush() {
        DWORD written;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buffer_, used_, &written, nullptr);
        used_ = 0;
    }
private:
    char buffer_[2048];
    DWORD used_{};
};

// Bounded last-resort evidence, not stack unwinding. Do not allocate, acquire
// application/CRT locks, or follow arbitrary pointers while handling a fault.
void PrintFaultContext(const CONTEXT& context) {
    FaultLine cpu;
    cpu.Text("{\"event\":\"fault_cpu_context\",\"thread_id_hex\":");
    cpu.Hex(GetCurrentThreadId());
    const auto reg = [&](const char* name, std::uint64_t value) {
        cpu.Text(",\""); cpu.Text(name); cpu.Text("\":"); cpu.Hex(value);
    };
    reg("rbx", context.Rbx); reg("rcx", context.Rcx); reg("rbp", context.Rbp);
    reg("r8", context.R8); reg("r9", context.R9); reg("r10", context.R10); reg("r11", context.R11);
    reg("r12", context.R12); reg("r13", context.R13); reg("r14", context.R14); reg("r15", context.R15);
    reg("eflags", context.EFlags);
    std::array<std::uint8_t, 32> instructions{};
    SIZE_T instruction_bytes{};
    const bool complete_code = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(context.Rip),
        instructions.data(), instructions.size(), &instruction_bytes) && instruction_bytes == instructions.size();
    instruction_bytes = std::min<SIZE_T>(instruction_bytes, instructions.size());
    cpu.Text(",\"instruction_bytes\":[");
    for (SIZE_T i = 0; i < instruction_bytes; ++i) { if (i) { cpu.Text(","); } cpu.Hex(instructions[i]); }
    cpu.Text("],\"instruction_sample_complete\":"); cpu.Text(complete_code ? "true" : "false");
    cpu.Text("}\n"); cpu.Flush();

    std::array<std::uint64_t, 128> stack{};
    SIZE_T stack_bytes{};
    const bool complete_stack = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(context.Rsp),
        stack.data(), sizeof(stack), &stack_bytes) && stack_bytes == sizeof(stack);
    const auto words = std::min<SIZE_T>(stack_bytes, sizeof(stack)) / sizeof(stack[0]);
    for (SIZE_T first = 0; first < words || first == 0; first += 16) {
        FaultLine sample;
        sample.Text("{\"event\":\"fault_stack_sample\",\"first_word_hex\":"); sample.Hex(first);
        sample.Text(",\"rsp\":"); sample.Hex(context.Rsp);
        sample.Text(",\"sample_complete\":"); sample.Text(complete_stack ? "true" : "false");
        sample.Text(",\"unwound\":false,\"words\":[");
        for (SIZE_T i = first; i < words && i - first < 16; ++i) { if (i != first) { sample.Text(","); } sample.Hex(stack[i]); }
        sample.Text("]}\n"); sample.Flush();
    }
    for (SIZE_T i = 0; i < words; ++i) {
        const auto* image = ImageAt(stack[i]);
        if (!image) { continue; }
        FaultLine candidate;
        candidate.Text("{\"event\":\"fault_stack_image_candidate\",\"word_index_hex\":"); candidate.Hex(i);
        candidate.Text(",\"address\":"); candidate.Hex(stack[i]);
        candidate.Text(",\"image\":"); candidate.Quote(image->image_name);
        candidate.Text(",\"image_offset\":"); candidate.Hex(stack[i] - reinterpret_cast<std::uintptr_t>(image->image));
        candidate.Text(",\"verified_return_address\":false}\n"); candidate.Flush();
    }
}

LONG WINAPI Unhandled(EXCEPTION_POINTERS* exception) {
    static std::atomic_flag handling = ATOMIC_FLAG_INIT;
    if (handling.test_and_set()) {
        TerminateProcess(GetCurrentProcess(), 22);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    PrintFaultContext(*exception->ContextRecord);
    const auto rip = exception->ContextRecord->Rip;
    const auto* runtime = ImageAt(rip);
    const auto base = runtime ? reinterpret_cast<std::uintptr_t>(runtime->image) : 0;
    FaultLine line;
    line.Text("{\"event\":\"unhandled_exception\",\"code\":");
    line.Hex(exception->ExceptionRecord->ExceptionCode);
    line.Text(",\"rip\":");
    line.Hex(rip);
    line.Text(",\"in_game_image\":");
    line.Text(runtime && rip >= base && rip - base < runtime->header.image_size ? "true" : "false");
    line.Text(",\"image_offset\":");
    line.Hex(rip - base);
    if (runtime) { line.Text(",\"caller_image\":"); line.Quote(runtime->image_name); }
    line.Text(",\"rsp\":");
    line.Hex(exception->ContextRecord->Rsp);
    line.Text(",\"rax\":");
    line.Hex(exception->ContextRecord->Rax);
    line.Text(",\"rdi\":");
    line.Hex(exception->ContextRecord->Rdi);
    line.Text(",\"rsi\":");
    line.Hex(exception->ContextRecord->Rsi);
    line.Text(",\"rdx\":");
    line.Hex(exception->ContextRecord->Rdx);
    if (active_runtime && exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        exception->ExceptionRecord->NumberParameters >= 2) {
        const auto fault = exception->ExceptionRecord->ExceptionInformation[1];
        line.Text(",\"fault_address\":");
        line.Hex(fault);
        line.Text(",\"access\":");
        line.Hex(exception->ExceptionRecord->ExceptionInformation[0]);
        line.Text("}\n");
        line.Flush();
        const auto guarded = [&](Runtime* candidate) {
            const auto base_guard = candidate ? reinterpret_cast<std::uintptr_t>(candidate->guards) : 0;
            return candidate && base_guard && fault >= base_guard && fault - base_guard < candidate->header.imports * kPage;
        };
        Runtime* guard_owner = guarded(active_runtime) ? active_runtime : nullptr;
        for (auto& entry : module_images) { auto* candidate = entry.load(std::memory_order_acquire); if (guarded(candidate)) { guard_owner = candidate; break; } }
        if (guard_owner) {
            const auto guards = reinterpret_cast<std::uintptr_t>(guard_owner->guards);
            const auto& import = guard_owner->imports[(fault - guards) / kPage];
            line.Text("{\"event\":\"unimplemented_data_import\",\"import_index_hex\":");
            line.Hex(static_cast<std::uint64_t>((fault - guards) / kPage));
            line.Text(",\"nid\":");
            line.Quote(import.nid);
            line.Text(",\"name\":");
            line.Quote(import.name);
            line.Text(",\"importing_image\":"); line.Quote(guard_owner->image_name);
            line.Text("}\n");
            line.Flush();
        }
    } else {
        line.Text("}\n");
        line.Flush();
    }
    TerminateProcess(GetCurrentProcess(), 22);
    return EXCEPTION_EXECUTE_HANDLER;
}

LONG CALLBACK ProbeFault(EXCEPTION_POINTERS* exception) {
    const auto code = exception->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION && exception->ExceptionRecord->NumberParameters >= 2 &&
        exception->ExceptionRecord->ExceptionInformation[0] == 1 && kt::memory::WriteTracker::HandleWriteFault(
            reinterpret_cast<void*>(exception->ExceptionRecord->ExceptionInformation[1]))) { return EXCEPTION_CONTINUE_EXECUTION; }
    if (active_runtime && active_runtime->entered &&
        (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION)) {
        return Unhandled(exception);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

bool Contains(const Runtime& runtime, std::uint64_t address, std::uint64_t bytes,
              bool executable) {
    for (const auto& segment : runtime.segments) {
        const auto extent = executable ? segment.file_size : segment.memory_size;
        if (address >= segment.address && bytes <= extent &&
            address - segment.address <= extent - bytes &&
            (!executable || (segment.flags & 1))) {
            return true;
        }
    }
    return false;
}

DWORD Protection(std::uint64_t flags) {
    if (flags & 1) {
        return (flags & 2) ? PAGE_EXECUTE_READWRITE : ((flags & 4) ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
    }
    return (flags & 2) ? PAGE_READWRITE : ((flags & 4) ? PAGE_READONLY : PAGE_NOACCESS);
}

std::uint64_t GuestExport(const Import& import) {
    std::lock_guard lock(module_gate);
    const auto nid = import.nid.substr(0, import.nid.find('#'));
    for (const auto& owner : active_runtime->modules) {
        for (const auto& entry : owner->exports) {
            const auto& symbol = entry.symbol;
            if (symbol.type == import.type && symbol.library == import.library && symbol.module == import.module &&
                symbol.nid.substr(0, symbol.nid.find('#')) == nid) {
                return reinterpret_cast<std::uint64_t>(owner->image + entry.offset);
            }
        }
    }
    return 0;
}

void RebindImports(Runtime& runtime) {
    unsigned functions = 0, data_slots = 0;
    for (std::size_t i = 0; i < runtime.imports.size(); ++i) {
        auto& import = runtime.imports[i];
        if (!import.pending) { continue; }
        const auto target = GuestExport(import); if (!target) { continue; }
        if (import.type == 2) {
            // The RX thunk always jumps through a separate RW target cell.
            // Updating it also fixes pointers copied from the GOT before the
            // module was loaded, without modifying published machine code.
            runtime.binding_cells[i].store(target, std::memory_order_release); ++functions;
        } else {
            for (const auto& relocation : runtime.import_relocations) {
                if (relocation.value != i) { continue; }
                auto* slot = runtime.image + relocation.address;
                if (reinterpret_cast<std::uintptr_t>(slot) % 8) { throw std::runtime_error("Unaligned late data relocation is unsupported"); }
                MEMORY_BASIC_INFORMATION memory{};
                if (!VirtualQuery(slot, &memory, sizeof(memory)) || (memory.Protect != PAGE_READONLY && memory.Protect != PAGE_READWRITE)) {
                    throw std::runtime_error("Late data relocation requires non-executable readable storage");
                }
                DWORD previous{};
                if (!VirtualProtect(slot, 8, PAGE_READWRITE, &previous)) { throw std::runtime_error("Cannot update late data relocation"); }
                const auto expected = import.address + static_cast<std::uint64_t>(relocation.addend);
                const auto value = target + static_cast<std::uint64_t>(relocation.addend);
                const auto old = InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), static_cast<LONG64>(value), static_cast<LONG64>(expected));
                DWORD unused{};
                if (!VirtualProtect(slot, 8, previous, &unused)) { throw std::runtime_error("Cannot restore late data relocation protection"); }
                if (static_cast<std::uint64_t>(old) == expected) { ++data_slots; }
            }
        }
        import.pending = false;
    }
    if (functions || data_slots) {
        std::printf("{\"event\":\"native_import_rebind\",\"importing_image\":%s,\"functions\":%u,\"data_slots\":%u}\n",
                    Quoted(runtime.image_name).c_str(), functions, data_slots);
    }
}

void Load(Runtime& runtime, Reader& reader, bool module = false) {
    runtime.header = reader.Value<Header>();
    runtime.is_module = module;
    const auto& header = runtime.header;
    if (std::memcmp(header.magic.data(), module ? "KTMODULE" : "KTPROBE1", 8) || (header.capabilities & ~1ull) ||
        !header.image_size || header.image_size > kMaximumImage || header.entry >= header.image_size ||
        !header.segments || header.segments > 32 || header.imports > 10000 ||
        header.relocations > 1000000 || header.procparam >= header.image_size) {
        throw std::runtime_error("Invalid native probe header");
    }
    if (module) {
        runtime.module_info = reader.Value<ModuleMetadata>();
        const auto& info = runtime.module_info;
        if (header.entry || header.procparam || !(info.flags & 1) || (info.flags & ~3ull) || info.exports > 10000) {
            throw std::runtime_error("Invalid no-TLS module metadata");
        }
    }
    for (std::uintptr_t preferred = 0x10000000; preferred <= 0x70000000; preferred += 0x10000000) {
        runtime.image = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(preferred),
                           static_cast<SIZE_T>(Align(header.image_size, 65536)), MEM_RESERVE, PAGE_NOACCESS));
        if (runtime.image) {
            break;
        }
    }
    if (!runtime.image) {
        throw std::runtime_error("Cannot reserve low-address game image");
    }
    std::uint64_t previous_end = 0;
    for (std::uint64_t i = 0; i < header.segments; ++i) {
        const auto segment = reader.Value<Segment>();
        if (segment.address % kPage || !segment.memory_size || segment.address < previous_end ||
            segment.file_size > segment.memory_size || segment.address >= header.image_size ||
            segment.memory_size > header.image_size - segment.address || segment.flags & ~7ull) {
            throw std::runtime_error("Invalid native probe segment");
        }
        if (!VirtualAlloc(runtime.image + segment.address, static_cast<SIZE_T>(Align(segment.memory_size, kPage)),
                          MEM_COMMIT, PAGE_READWRITE)) {
            throw std::runtime_error("Cannot commit game segment");
        }
        reader.Read(runtime.image + segment.address, static_cast<std::size_t>(segment.file_size));
        runtime.segments.push_back(segment);
        previous_end = Align(segment.address + segment.memory_size, kPage);
    }
    if ((!module && !Contains(runtime, header.entry, 1, true)) ||
        (header.procparam && !Contains(runtime, header.procparam, 8))) {
        throw std::runtime_error("Entry or process parameters outside mapped segments");
    }
    if (module && (!Contains(runtime, runtime.module_info.init, 1, true) ||
        ((runtime.module_info.flags & 2) && !Contains(runtime, runtime.module_info.fini, 1, true)))) {
        throw std::runtime_error("Module initializer/finalizer outside executable file bytes");
    }
    runtime.imports.reserve(static_cast<std::size_t>(header.imports));
    for (std::uint64_t i = 0; i < header.imports; ++i) {
        Import import{reader.String(), reader.String(), reader.String(), reader.String(), reader.Value<std::uint32_t>()};
        if (import.nid.empty() || (import.type != 1 && import.type != 2)) {
            throw std::runtime_error("Unsupported import symbol kind");
        }
        runtime.imports.push_back(std::move(import));
    }
    if (module) {
        runtime.exports.reserve(static_cast<std::size_t>(runtime.module_info.exports));
        for (std::uint64_t i = 0; i < runtime.module_info.exports; ++i) {
            Export item{{reader.String(), reader.String(), reader.String(), reader.String(), reader.Value<std::uint32_t>()},
                        reader.Value<std::uint32_t>(), reader.Value<std::uint64_t>(), reader.Value<std::uint64_t>()};
            if (item.symbol.nid.empty() || item.symbol.library.empty() || item.symbol.module.empty() ||
                (item.symbol.type != 1 && item.symbol.type != 2) || (item.binding != 1 && item.binding != 2) ||
                !Contains(runtime, item.offset, (std::max)(1ull, item.size), item.symbol.type == 2)) {
                throw std::runtime_error("Invalid module export");
            }
            for (const auto& prior : runtime.exports) {
                if (prior.symbol.nid.substr(0, prior.symbol.nid.find('#')) == item.symbol.nid.substr(0, item.symbol.nid.find('#')) &&
                    prior.symbol.library == item.symbol.library && prior.symbol.module == item.symbol.module) {
                    throw std::runtime_error("Duplicate module export");
                }
            }
            runtime.exports.push_back(std::move(item));
        }
    }
    if (module) {
        runtime.stack_guard = active_runtime->stack_guard;
        runtime.owns_guard = false;
    } else {
    runtime.stack_guard = static_cast<std::uint64_t*>(VirtualAlloc(nullptr, kPage,
                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!runtime.stack_guard || BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(runtime.stack_guard),
                                               sizeof(*runtime.stack_guard), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
        !*runtime.stack_guard) {
        throw std::runtime_error("Cannot initialize the stack-protection canary");
    }
    DWORD old_guard_protection;
    if (!VirtualProtect(runtime.stack_guard, kPage, PAGE_READONLY, &old_guard_protection)) {
        throw std::runtime_error("Cannot protect the stack-protection canary");
    }
    }
    if (header.imports) {
        const auto text_bytes = Align(header.imports * kThunkBytes, kPage);
        runtime.thunks = static_cast<unsigned char*>(VirtualAlloc(nullptr, static_cast<SIZE_T>(text_bytes + Align(header.imports * 8, kPage)),
                               MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        runtime.guards = static_cast<unsigned char*>(VirtualAlloc(nullptr, static_cast<SIZE_T>(header.imports * kPage),
                               MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS));
        if (!runtime.thunks || !runtime.guards) {
            throw std::runtime_error("Cannot allocate import diagnostic regions");
        }
        runtime.binding_cells = reinterpret_cast<std::atomic<std::uint64_t>*>(runtime.thunks + text_bytes);
    }
    for (std::size_t i = 0; i < runtime.imports.size(); ++i) {
        auto& import = runtime.imports[i];
        import.address = GuestExport(import);
        if (import.address) { continue; }
        if (import.type == 2) {
            import.address = ResolveGnmImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveSystemStatusImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveRtcImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveFiosImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveFileImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveContentImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveNgs2Import(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveAvPlayerImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveAudioImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveNpImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolvePadImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveVideoImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveStorageImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveScreenshotImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveUserImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveDialogImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
        }
        if (import.library == "libkernel" && import.module == "libkernel") {
            if (import.type == 2) {
                if (import.nid.starts_with("wzvqT4UqKX8#")) { import.address = reinterpret_cast<std::uint64_t>(&NativeLoadStartModule); continue; }
                if (import.nid.starts_with("LwG8g3niqwA#")) { import.address = reinterpret_cast<std::uint64_t>(&NativeDlsym); continue; }
                import.address = ResolveProfileImport(import.nid);
                if (import.address) { continue; }
                import.address = ResolveEqueueImport(import.nid);
                if (import.address) { continue; }
                import.address = ResolveMemoryImport(import.nid);
                if (import.address) { continue; }
                import.address = ResolveTimeImport(import.nid);
                if (import.address) { continue; }
                import.address = ResolveMutexImport(import.nid);
                if (import.address) { continue; }
                import.address = ResolveSemaphoreImport(import.nid);
                if (import.address) { continue; }
                import.address = ResolveEventFlagImport(import.nid, import.library, import.module);
                if (import.address) { continue; }
                import.address = ResolveThreadImport(import.nid);
                if (import.address) { continue; }
            }
            if (import.type == 1 && import.nid.starts_with("f7uOxY9mM1U#")) {
                import.address = reinterpret_cast<std::uint64_t>(runtime.stack_guard);
                continue;
            }
            if (import.type == 2 && import.nid.starts_with("Ou3iL1abvng#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeStackCheckFailure);
                continue;
            }
            if (import.type == 2 && import.nid.starts_with("9BcDykPmo1I#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeError);
                continue;
            }
        }
        if ((header.capabilities & 1) && import.type == 2 && import.library == "libc" &&
            import.module == "libc" && import.nid.starts_with("bzQExy189ZI#")) {
            import.address = reinterpret_cast<std::uint64_t>(&VerifiedInitEnv);
            continue;
        }
        if (import.type == 1 && import.library == "libc" && import.module == "libc" &&
            import.nid.starts_with("NLwJ3q+64bY#")) {
            import.address = reinterpret_cast<std::uint64_t>(&kNoThrowTag);
            continue;
        }
        if (import.type == 2 && import.library == "libc" && import.module == "libc") {
            import.address = ResolveLibcCalendarImport(import.nid, import.library, import.module);
            if (import.address) { continue; }
            import.address = ResolveMathImport(import.nid);
            if (import.address) { continue; }
            import.address = ResolveCxxGuardImport(import.nid);
            if (import.address) { continue; }
            import.address = ResolveLibcMemoryImport(import.nid);
            if (import.address) { continue; }
            import.address = ResolveTextImport(import.nid);
            if (import.address) { continue; }
            import.address = ResolveFormatImport(import.nid);
            if (import.address) { continue; }
            import.address = ResolveMspaceImport(import.nid);
            if (import.address) { continue; }
            if (import.nid.starts_with("gQX+4GDQjpM#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeMalloc);
                continue;
            }
            if (import.nid.starts_with("2X5agFjKxMc#")) {
                import.address=reinterpret_cast<std::uint64_t>(&NativeCalloc);continue;
            }
            if (import.nid.starts_with("Ujf3KzMvRmI#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeMemalign);
                continue;
            }
            if (import.nid.starts_with("tIhsqj0qsFE#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeDelete);
                continue;
            }
            if (import.nid.starts_with("tsvEmnenz48#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeCxaAtexit);
                continue;
            }
            if (import.nid.starts_with("H2e8t5ScQGc#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeCxaFinalize);
                continue;
            }
            if (import.nid.starts_with("fJnpuVVBbKk#") || import.nid.starts_with("hdm0YfMa7TQ#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeNew);
                continue;
            }
            if (import.nid.starts_with("ryUxD-60bKM#") || import.nid.starts_with("Jh5qUcwiSEk#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeNewNoThrow);
                continue;
            }
            if (import.nid.starts_with("z+P+xCnWLBk#") || import.nid.starts_with("MLWl90SFWNE#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeDelete);
                continue;
            }
            if (import.nid.starts_with("8G2LB+A3rzg#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeAtexit);
                continue;
            }
            if (import.nid.starts_with("uMei1W9uyNo#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeExit);
                continue;
            }
            if (import.nid.starts_with("wZi5ly2guNw#")) {
                import.address = reinterpret_cast<std::uint64_t>(&NativeImmediateExit);
                continue;
            }
        }
        import.pending = true;
        if (import.type != 2) {
            import.address = reinterpret_cast<std::uint64_t>(runtime.guards + i * kPage);
            continue;
        }
        // Snapshot the six integer/pointer registers before rearranging the
        // diagnostic call. RSP is 16-byte aligned before CALL; the caller's
        // original return address stays at [rsp+0x38]. This path never returns.
        constexpr std::array<unsigned char, 65> instructions{
            0x48, 0x83, 0xec, 0x38,
            0x48, 0x89, 0x3c, 0x24,
            0x48, 0x89, 0x74, 0x24, 0x08,
            0x48, 0x89, 0x54, 0x24, 0x10,
            0x48, 0x89, 0x4c, 0x24, 0x18,
            0x4c, 0x89, 0x44, 0x24, 0x20,
            0x4c, 0x89, 0x4c, 0x24, 0x28,
            0x48, 0xbf, 0, 0, 0, 0, 0, 0, 0, 0,
            0x48, 0x8b, 0x74, 0x24, 0x38,
            0x48, 0x89, 0xe2,
            0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,
            0xff, 0xd0, 0x0f, 0x0b};
        static_assert(16 + instructions.size() <= kThunkBytes);
        auto* entry = runtime.thunks + i * kThunkBytes;
        auto* thunk = entry + 16;
        entry[0] = 0xff; entry[1] = 0x25;
        const auto displacement = static_cast<std::int32_t>(reinterpret_cast<unsigned char*>(&runtime.binding_cells[i]) - (entry + 6));
        std::memcpy(entry + 2, &displacement, 4);
        ::new (&runtime.binding_cells[i]) std::atomic<std::uint64_t>(reinterpret_cast<std::uint64_t>(thunk));
        std::memcpy(thunk, instructions.data(), instructions.size());
        const auto descriptor = reinterpret_cast<std::uint64_t>(&import);
        const auto handler = reinterpret_cast<std::uint64_t>(&MissingImport);
        std::memcpy(thunk + 35, &descriptor, 8);
        std::memcpy(thunk + 53, &handler, 8);
        import.address = reinterpret_cast<std::uint64_t>(entry);
    }
    for (std::uint64_t i = 0; i < header.relocations; ++i) {
        const auto relocation = reader.Value<Relocation>();
        if (!Contains(runtime, relocation.address, 8) || relocation.kind > 2 ||
            (relocation.kind == 1 && relocation.value >= runtime.imports.size())) {
            throw std::runtime_error("Invalid native probe relocation");
        }
        std::uint64_t value = relocation.value;
        if (relocation.kind == 0) {
            value += reinterpret_cast<std::uint64_t>(runtime.image);
        } else if (relocation.kind == 1) {
            value = runtime.imports[static_cast<std::size_t>(value)].address;
            runtime.import_relocations.push_back(relocation);
        }
        value += static_cast<std::uint64_t>(relocation.addend);
        std::memcpy(runtime.image + relocation.address, &value, 8);
    }
    reader.End();
    if (!runtime.is_module) {
        InstallNativeCommunityRoundAudit(runtime.image, static_cast<std::size_t>(header.image_size), header.source_sha256);
        InstallNativeCommunityRounds(runtime.image, static_cast<std::size_t>(header.image_size), header.source_sha256);
    }
    for (const auto& segment : runtime.segments) {
        DWORD previous;
        if (!VirtualProtect(runtime.image + segment.address, static_cast<SIZE_T>(Align(segment.memory_size, kPage)),
                            Protection(segment.flags), &previous)) {
            throw std::runtime_error("Cannot protect game segment");
        }
    }
    if (header.imports) {
        DWORD previous;
        if (!VirtualProtect(runtime.thunks, static_cast<SIZE_T>(Align(header.imports * kThunkBytes, kPage)), PAGE_EXECUTE_READ, &previous)) {
            throw std::runtime_error("Cannot protect import thunks");
        }
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    std::printf("{\"event\":\"image_prepared\",\"image_base\":\"%p\",\"image_bytes\":%llu,"
                "\"entry_offset\":\"0x%llx\",\"imports\":%llu,\"relocations\":%llu,\"image_name\":%s,\"source_sha256\":\"",
                static_cast<void*>(runtime.image), header.image_size, header.entry, header.imports, header.relocations, Quoted(runtime.image_name).c_str());
    for (const auto byte : header.source_sha256) {
        std::printf("%02x", byte);
    }
    std::puts("\"}");
}

void KT_SYSV ProgramExit() {
    std::puts("{\"event\":\"game_exit_callback\"}");
}

bool ModulePath(const char* text, std::string& name) {
    if (!text || strnlen_s(text, 4097) > 4096) { return false; }
    const std::string_view path(text);
    if (!path.starts_with("/app0/")) { return false; }
    const auto base = path.substr(6);
    if (base.empty() || base.size() > 240 || base == "." || base == ".." ||
        base.find_first_of("/\\:") != std::string_view::npos || !base.ends_with(".prx")) { return false; }
    for (auto c : base) { if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) >= 127 || c == '<' || c == '>' || c == '?' || c == '*' || c == '"') { return false; } }
    name.assign(base);
    return true;
}
void CheckModulePath(const std::filesystem::path& path) {
    for (auto cursor = path; !cursor.empty();) {
        const auto attributes = GetFileAttributesW(cursor.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            throw std::runtime_error("Missing or reparse-point module path");
        }
        const auto parent = cursor.parent_path();
        if (parent == cursor) { break; }
        cursor = parent;
    }
}
void VerifyModuleSource(const Runtime& module) {
    if (!NativeContentConfigured()) { return; }
    auto source = OpenNativeContentFile("/app0/" + module.image_name);
    if (!source) { throw std::runtime_error("Prepared module has no source in the selected content view"); }
    struct Hash { BCRYPT_HASH_HANDLE value{}; ~Hash() { if (value) { BCryptDestroyHash(value); } } } hash;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash.value, nullptr, 0, nullptr, 0, 0) < 0) { throw std::runtime_error("Cannot hash selected module source"); }
    std::vector<std::byte> buffer(64 * 1024);
    for (std::uint64_t offset = 0; offset < source->Size();) {
        const auto read = source->ReadAt(offset, buffer);
        if (!read || BCryptHashData(hash.value, reinterpret_cast<unsigned char*>(buffer.data()), static_cast<ULONG>(read), 0) < 0) {
            throw std::runtime_error("Cannot hash selected module source");
        }
        offset += read;
    }
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash.value, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0 || digest != module.header.source_sha256) {
        throw std::runtime_error("Prepared module differs from the patch-first content view; prepare the selected original module");
    }
    std::printf("{\"event\":\"native_module_source_verified\",\"image_name\":%s,\"patch_first_view\":true}\n", Quoted(module.image_name).c_str());
}

void CensusBindings(Runtime& main) {
    // Use the real loader/resolvers, but never call an entry, initializer or
    // imported function. Module exports are availability, not initialized state.
    std::vector<std::filesystem::path> paths;
    if (!module_directory.empty()) {
        for (const auto& entry : std::filesystem::directory_iterator(module_directory)) {
            if (entry.path().extension() != L".ktmodule") { continue; }
            CheckModulePath(entry.path());
            if (!entry.is_regular_file()) { throw std::runtime_error("Census module is not a regular file"); }
            paths.push_back(entry.path());
            if (paths.size() > module_images.size()) { throw std::runtime_error("Too many census modules"); }
        }
        std::sort(paths.begin(), paths.end());
    }
    for (const auto& path : paths) {
        const auto name = path.stem().string();
        std::string checked;
        if (!ModulePath(("/app0/" + name).c_str(), checked)) { throw std::runtime_error("Invalid census module name"); }
        for (const auto& owner : main.modules) {
            if (!_stricmp(owner->image_name.c_str(), name.c_str())) { throw std::runtime_error("Duplicate census module name"); }
        }
        auto candidate = std::make_unique<Runtime>();
        candidate->image_name = name;
        Reader reader(path);
        Load(*candidate, reader, true);
        main.modules.push_back(std::move(candidate));
    }
    std::size_t host = 0, module = 0, unbound = 0;
    const auto inspect = [&](const Runtime& image) {
        for (std::size_t i = 0; i < image.imports.size(); ++i) {
            const auto& import = image.imports[i];
            const auto target = GuestExport(import);
            const char* status;
            if (target && (import.pending || import.address == target)) {
                status = "module_export_available"; ++module;
            } else if (!import.pending) {
                status = "host_bound"; ++host;
            } else {
                status = "unbound"; ++unbound;
            }
            std::printf("{\"event\":\"native_binding_census_import\",\"image_name\":%s,\"index\":%zu,"
                        "\"nid\":%s,\"name\":%s,\"library\":%s,\"module\":%s,\"type\":%u,\"status\":\"%s\"}\n",
                        Quoted(image.image_name).c_str(), i, Quoted(import.nid).c_str(), Quoted(import.name).c_str(),
                        Quoted(import.library).c_str(), Quoted(import.module).c_str(), import.type, status);
        }
    };
    inspect(main);
    for (const auto& owner : main.modules) { inspect(*owner); }
    std::printf("{\"event\":\"native_binding_census_complete\",\"images\":%zu,\"imports\":%zu,"
                "\"host_bound\":%zu,\"module_export_available\":%zu,\"unbound\":%zu,"
                "\"game_entry_reached\":false,\"module_initializers_called\":false,\"capability_validation\":false}\n",
                main.modules.size() + 1, host + module + unbound, host, module, unbound);
}
__declspec(noinline) int LoadStartModuleImpl(const char* path, std::uint64_t bytes, const void* argument,
                        unsigned flags, const void* options, int* result, const void* caller) {
    bool published = false;
    try {
        std::string name;
        if (!ModulePath(path, name) || (bytes && !argument) || bytes > 1024 * 1024) { return static_cast<int>(0x80020016u); }
        if (flags || options) { UnsupportedApi("sceKernelLoadStartModule_options", flags, caller); }
        if (module_directory.empty()) { UnsupportedApi("sceKernelLoadStartModule_no_prepared_directory", 0, caller); }
        std::lock_guard lock(module_gate);
        auto& owners = active_runtime->modules;
        for (const auto& item : owners) {
            if (_stricmp(item->image_name.c_str(), name.c_str())) { continue; }
            if (!item->started) { UnsupportedApi("native_module_recursive_start", static_cast<unsigned>(item->module_handle), caller); }
            if (result) { *result = item->start_result; }
            return item->module_handle;
        }
        if (owners.size() == module_images.size()) { return static_cast<int>(0x8002000cu); }
        const auto input = module_directory / std::filesystem::path(name + ".ktmodule");
        if (!std::filesystem::exists(input)) {
            if (NativeContentConfigured() && OpenNativeContentFile("/app0/" + name)) {
                UnsupportedApi("native_module_preparation_required", 0, caller);
            }
            return static_cast<int>(0x80020002u);
        }
        CheckModulePath(input);
        auto candidate = std::make_unique<Runtime>();
        candidate->image_name = name;
        Reader reader(input);
        Load(*candidate, reader, true);
        VerifyModuleSource(*candidate);
        candidate->module_handle = 0x100 + static_cast<int>(owners.size());
        auto* module = candidate.get();
        const auto slot = owners.size();
        owners.push_back(std::move(candidate));
        module_images[slot].store(module, std::memory_order_release);
        published = true;
        std::printf("{\"event\":\"native_module_start\",\"image_name\":%s,\"handle\":%d,\"image_base\":\"%p\",\"init_offset\":\"0x%llx\",\"exports\":%zu}\n",
            Quoted(name).c_str(), module->module_handle, static_cast<void*>(module->image), module->module_info.init, module->exports.size());
        using Initializer = int (KT_SYSV *)(std::uint64_t, const void*, void*);
        module->start_result = reinterpret_cast<Initializer>(module->image + module->module_info.init)(bytes, argument, nullptr);
        module->started = true;
        RebindImports(*active_runtime);
        for (const auto& owner : owners) { RebindImports(*owner); }
        if (result) { *result = module->start_result; }
        TraceNativeApi("sceKernelLoadStartModule", static_cast<unsigned>(module->module_handle), caller);
        return module->module_handle;
    } catch (const std::bad_alloc&) {
        if (published) { UnsupportedApi("native_module_post_publish_allocation_failure", 0, caller); }
        return static_cast<int>(0x8002000cu);
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Native module load error: %s\n", error.what());
        UnsupportedApi("native_module_load_failure", 0, caller);
    }
}
int KT_SYSV NativeLoadStartModule(const char* path, std::uint64_t bytes, const void* argument,
                                 unsigned flags, const void* options, int* result) {
    // All host exception handling stays in an ordinary Windows-ABI frame.
    return LoadStartModuleImpl(path, bytes, argument, flags, options, result, __builtin_return_address(0));
}

std::string SymbolNid(std::string_view name) {
    constexpr std::array<unsigned char, 16> salt{0x51, 0x8d, 0x64, 0xa6, 0x35, 0xde, 0xd8, 0xc1, 0xe6, 0xb0, 0x39, 0xb1, 0xc3, 0xe5, 0x52, 0x30};
    std::vector<unsigned char> input(name.begin(), name.end());
    input.insert(input.end(), salt.begin(), salt.end());
    std::array<unsigned char, 20> digest{};
    if (BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0, input.data(), static_cast<ULONG>(input.size()), digest.data(),
                   static_cast<ULONG>(digest.size())) < 0) { throw std::runtime_error("Cannot hash a module symbol"); }
    std::uint64_t value{};
    for (unsigned i = 0; i < 8; ++i) { value |= static_cast<std::uint64_t>(digest[i]) << (8 * i); }
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    std::string result;
    for (int shift = 58; shift >= 4; shift -= 6) { result += alphabet[(value >> shift) & 63]; }
    result += alphabet[(value & 15) << 2];
    return result;
}
__declspec(noinline) int DlsymImpl(int handle, const char* name, void** output, const void* caller) {
    try {
        if (!output) { return static_cast<int>(0x8002000eu); }
        if (!name || !*name || strnlen_s(name, 4097) > 4096) { return static_cast<int>(0x80020016u); }
        std::lock_guard lock(module_gate);
        for (const auto& module : active_runtime->modules) {
            if (module->module_handle != handle) { continue; }
            const auto hashed = SymbolNid(name);
            void* found{};
            for (const auto& entry : module->exports) {
                const auto id = entry.symbol.nid.substr(0, entry.symbol.nid.find('#'));
                if (id != name && id != hashed) { continue; }
                const auto address = module->image + entry.offset;
                if (found && found != address) { UnsupportedApi("sceKernelDlsym_ambiguous_export", static_cast<unsigned>(handle), caller); }
                found = address;
            }
            *output = found;
            TraceNativeApi("sceKernelDlsym", static_cast<unsigned>(handle), caller);
            return found ? 0 : static_cast<int>(0x80020003u);
        }
        return static_cast<int>(0x80020003u);
    } catch (...) { UnsupportedApi("native_module_symbol_failure", static_cast<unsigned>(handle), caller); }
}
int KT_SYSV NativeDlsym(int handle, const char* name, void** output) {
    return DlsymImpl(handle, name, output, __builtin_return_address(0));
}

std::uint64_t KT_SYSV AbiFixture(std::uint64_t a, std::uint64_t b, std::uint64_t c,
                                std::uint64_t d, std::uint64_t e, std::uint64_t f) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f;
}

} // namespace

void SetNativeErrno(int value) { native_errno = value; }
bool NativeExecutableAddress(std::uintptr_t address) { return AcceptCallback(address); }

[[noreturn]] void UnsupportedApi(const char* name, std::uint64_t detail, const void* caller) {
    HoldTerminalOutput();
    const auto address = reinterpret_cast<std::uintptr_t>(caller);
    const auto* image = ImageAt(address);
    const auto base = image ? reinterpret_cast<std::uintptr_t>(image->image) : 0;
    PrintNativeFileDiagnostics();
    PrintNativeHostTimingDiagnostics();
    std::printf("{\"event\":\"unimplemented_api\",\"name\":%s,\"detail\":\"0x%llx\","
                "\"caller_offset\":\"0x%llx\",\"in_game_image\":%s,\"caller_image\":%s}\n", Quoted(name).c_str(), detail,
                static_cast<unsigned long long>(address - base),
                image ? "true" : "false", image ? Quoted(image->image_name).c_str() : "null");
    std::fflush(stdout);
    if (terminal_log_test.load()) {
        terminal_log_announced.store(true, std::memory_order_release);
        Sleep(50); // deterministic regression window, enabled only by the self-test mode
    }
    TerminateProcess(GetCurrentProcess(), 20);
    ExitProcess(20);
}

[[noreturn]] void StopForNativeWindowClose(const char* stage, unsigned video, unsigned buffer, const void* caller) {
    // Every caller has left its submission scopes. Only one owns process
    // teardown; later cancelled submitters park without holding host locks.
    if (requested_close_owner.test_and_set(std::memory_order_acq_rel)) { for (;;) { Sleep(INFINITE); } }
    std::printf("{\"event\":\"native_shutdown_begin\",\"reason\":\"window_close_requested\",\"stage\":%s,\"video_handle\":%u}\n", Quoted(stage).c_str(), video);
    try {
        if (!CompleteNativePresentationShutdown(static_cast<int>(video))) { UnsupportedApi("native_shutdown_presentation_state", video, caller); }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Presentation shutdown failed: %s\n", error.what());
        UnsupportedApi("native_shutdown_presentation_failure", video, caller);
    } catch (...) { UnsupportedApi("native_shutdown_presentation_host_failure", video, caller); }
    // Do not own stdout while joining the UI/GPU threads: they may need to
    // finish a diagnostic write before releasing a lifecycle lock.
    HoldTerminalOutput();
    const auto address = reinterpret_cast<std::uintptr_t>(caller);
    const auto* image = ImageAt(address);
    const auto base = image ? reinterpret_cast<std::uintptr_t>(image->image) : 0;
    PrintNativeFileDiagnostics();
    PrintNativeHostTimingDiagnostics();
    // Submission's host scopes have already returned. Never dispatch guest
    // finalizers, free its images, or unwind through an ELF frame here: other
    // guest threads and callbacks have not yet been quiesced.
    std::printf("{\"event\":\"native_shutdown_incomplete\",\"reason\":\"window_close_requested\",\"stage\":%s,"
                "\"video_handle\":%u,\"buffer\":%u,\"guest_finalizers_run\":false,\"host_cleanup_complete\":false,"
                "\"presentation_cleanup_complete\":true,"
                "\"exit_code\":27,\"caller_offset\":\"0x%llx\",\"caller_image\":%s}\n",
                Quoted(stage).c_str(), video, buffer, static_cast<unsigned long long>(address-base),
                image ? Quoted(image->image_name).c_str() : "null");
    std::fflush(stdout);
    cave::ps4::RecordCollectionWindowClose();
    if (terminal_log_test.load()) { terminal_log_announced.store(true, std::memory_order_release); Sleep(50); }
    TerminateProcess(GetCurrentProcess(), 27);
    ExitProcess(27);
}

void TraceNativeApi(const char* name, std::uint64_t value, const void* caller) {
    if (!active_runtime || !active_runtime->entered) { return; }
    const auto address = reinterpret_cast<std::uintptr_t>(caller);
    const auto* image = ImageAt(address);
    const auto base = image ? reinterpret_cast<std::uintptr_t>(image->image) : 0;
    NativeLogf("{\"event\":\"native_api_call\",\"name\":%s,\"value\":%llu,"
                "\"caller_offset\":\"0x%llx\",\"in_game_image\":%s,\"caller_image\":%s}\n", Quoted(name).c_str(), value,
                static_cast<unsigned long long>(address - base),
                image ? "true" : "false", image ? Quoted(image->image_name).c_str() : "null");
}

bool EmitNativeHostOperation(const char* name, const char* category, std::uint64_t detail,
        const void* caller, std::uint64_t began, std::uint64_t completed, std::uint64_t suppressed) noexcept {
    try {
        const auto address = reinterpret_cast<std::uintptr_t>(caller);
        const auto* image = ImageAt(address);
        const auto base = image ? reinterpret_cast<std::uintptr_t>(image->image) : 0;
        return NativeLogf("{\"event\":\"native_host_operation\",\"name\":%s,\"category\":%s,\"detail\":%llu,\"began_clock_us\":%llu,\"clock_us\":%llu,\"duration_us\":%llu,\"thread\":%lu,\"caller_offset\":\"0x%llx\",\"caller_image\":%s,\"suppressed_since_last_record\":%llu}\n",
            Quoted(name).c_str(), Quoted(category).c_str(), detail, began, completed, completed - began,
            GetCurrentThreadId(), static_cast<unsigned long long>(address - base),
            image ? Quoted(image->image_name).c_str() : "null", suppressed) >= 0;
    } catch (...) { /* Timing must never change the operation's result. */ }
    return false;
}

int NativeCommand(int argc, wchar_t** argv) {
    if (argc==2 && std::wstring_view(argv[1])==L"--math-self-test") return MathSelfTest()?0:1;
    if (argc>=2 && std::wstring(argv[1])==L"--prepare-elf") return cave::ps4::PrepareElfCommand(argc,argv);
    if (argc>=2 && std::wstring(argv[1])==L"--prepare-fixes") return cave::ps4::PrepareReadFixesCommand(argc,argv);
    if (argc>=2 && std::wstring(argv[1])==L"--prepare-game") return cave::ps4::PrepareGameCommand(argc,argv);
    if (argc==2 && std::wstring(argv[1])==L"--collection-config-self-test") {
        try { return cave::ps4::CollectionConfigurationSelfTest() ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr,"Collection config: %s\n",error.what()); return 1; }
    }
    if (argc==2 && std::wstring(argv[1])==L"--collection-input-self-test") {
        try {return cave::ps4::CollectionInputDiagnosticSelfTest()?0:1;}
        catch (const std::exception& error) {std::fprintf(stderr,"Collection input diagnostic: %s\n",error.what());return 2;}
    }
    if (argc==3 && std::wstring(argv[1])==L"--collection-config-check") {
        try { cave::ps4::ReportCollectionConfiguration(cave::ps4::LoadCollectionConfiguration(argv[2])); return 0; }
        catch (const std::exception& error) { std::fprintf(stderr,"Collection config: %s\n",error.what()); return 2; }
    }
    if (argc==3 && std::wstring(argv[1])==L"--waveform-check") { return Ngs2WaveformCheck(argv[2]); }
    // Set this before creating any window or Vulkan surface. Otherwise Win32
    // client coordinates and the driver's surface extent can use different
    // DPI spaces, shrinking the image into the top-left of the real window.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
        !AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        std::fprintf(stderr, "Could not establish per-monitor-v2 DPI awareness: %lu\n", GetLastError()); return 2;
    }
    SetConsoleOutputCP(CP_UTF8);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 2 && std::wstring(argv[1]) == L"--host-timing-self-test") { return NativeHostTimingSelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--host-timing-counts-self-test") { return NativeHostCountsOnlySelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--host-timing-default-self-test") { return NativeHostCountsOnlySelfTest(false) ? 0 : 1; }
    if (argc==2 && std::wstring(argv[1])==L"--gamepad-self-test") {
        try { return kt::input::SdlGamepadSelfTest()?0:1; }
        catch (const std::exception& error) { std::fprintf(stderr,"SDL gamepad test: %s\n",error.what()); return 2; }
    }
    if ((argc==2 && std::wstring(argv[1])==L"--gamepad-list") ||
        (argc==3 && std::wstring(argv[1])==L"--gamepad-monitor")) {
        try {
            unsigned seconds=0;
            if (argc==3) {
                const std::wstring value(argv[2]);
                if (value.empty() || value.size()>3 || value.find_first_not_of(L"0123456789")!=std::wstring::npos ||
                    (seconds=static_cast<unsigned>(std::stoul(value)))>180) throw std::runtime_error("Monitor duration must be 0..180 seconds");
            }
            return kt::input::SdlGamepadMonitor(seconds);
        } catch (const std::exception& error) { std::fprintf(stderr,"SDL gamepad monitor: %s\n",error.what()); return 2; }
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--input-config-check") {
        try { LoadKeyboardConfiguration(argv[2]); return 0; }
        catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 2; }
    }
    if (argc >= 2 && std::wstring(argv[1]) == L"--pm4-journal") {
        if (argc != 3) { std::fputs("Usage: KetsuiNativeProbe --pm4-journal stream.bin\n", stderr); return 2; }
        try {
            const auto bytes = std::filesystem::file_size(argv[2]);
            if (bytes % 4 || bytes > 16384 * sizeof(std::uint32_t)) { throw std::runtime_error("PM4 input must contain at most 16384 whole little-endian DWORDs"); }
            Reader input{argv[2]};
            std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes / 4));
            if (bytes) { input.Read(words.data(), static_cast<std::size_t>(bytes)); }
            input.End();
            const auto journal = kt::gfx::pm4::Analyze(words);
            std::printf("{\"event\":\"native_pm4_journal\",\"dwords\":%zu,\"complete\":%s,\"packets\":%u,\"draws\":%u,\"acquires\":%u,\"error\":%u,\"error_offset\":%u,\"error_words\":[",
                words.size(), journal.Complete() ? "true" : "false", journal.packets, journal.draws, journal.acquires,
                static_cast<unsigned>(journal.error), journal.error_offset);
            if (!journal.Complete()) {
                const auto end = (std::min)(words.size(), std::size_t{journal.error_offset} + 16);
                for (auto index = std::size_t{journal.error_offset}; index < end; ++index) {
                    std::printf(index == journal.error_offset ? "%u" : ",%u", words[index]);
                }
            }
            std::fputs("],\"last_acquire\":", stdout);
            if (journal.last_acquire) {
                const auto& acquire = *journal.last_acquire;
                std::printf("{\"control\":%u,\"base\":%llu,\"bytes\":%llu,\"interval\":%u}", acquire.control, acquire.base, acquire.bytes, acquire.interval);
            } else { std::fputs("null", stdout); }
            std::puts(",\"gpu_executed\":false,\"game_entry_reached\":false}");
            return journal.Complete() ? 0 : 20;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "[PM4 journal] %s\n", error.what()); return 2;
        }
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--sample-process") {
        wchar_t* end{}; const auto process = wcstoul(argv[2], &end, 10);
        if (!*argv[2] || !end || *end || !process || process == ULONG_MAX) { return 2; }
        return SampleNativeProcess(process);
    }
    if (argc == 2 && (std::wstring(argv[1]) == L"--terminal-log-self-test" || std::wstring(argv[1]) == L"--terminal-close-log-self-test")) {
        if (!StartNativeAsyncLog()) return 2;
        for (unsigned index = 0; index < 32; ++index) (void)NativeLogf("{\"event\":\"terminal_queue_fixture\",\"index\":%u}\n", index);
        terminal_log_test = true;
        const auto writer = CreateThread(nullptr, 0, TerminalTestWriter, nullptr, 0, nullptr);
        if (!writer) { return 2; }
        CloseHandle(writer);
        if (std::wstring(argv[1]) == L"--terminal-close-log-self-test") { StopForNativeWindowClose("self_test", 0, 0, nullptr); }
        UnsupportedApi("terminal_log_self_test", 0, nullptr);
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--async-log-self-test") { return NativeAsyncLogSelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--pm4-self-test") { return kt::gfx::pm4::SelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--sfo-self-test") { return kt::content::SfoSelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--condition-self-test") { return ConditionSelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--semaphore-self-test") { return SemaphoreSelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--event-flag-self-test") { return EventFlagSelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--libc-memory-self-test") { return LibcMemorySelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--text-self-test") { return TextSelfTest() ? 0 : 1; }
    if (argc == 4 && std::wstring(argv[1]) == L"--media-self-test") { return AvPlayerMediaSelfTest(argv[2], argv[3]) ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--thread-affinity-self-test") { return ThreadAffinitySelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--np-request-self-test") { return NpRequestSelfTest() ? 0 : 1; }
    if (argc == 5 && std::wstring(argv[1]) == L"--trophy-self-test" && (std::wstring(argv[4])==L"write" || std::wstring(argv[4])==L"read")) {
        return TrophySelfTest(argv[2],argv[3],std::wstring(argv[4])==L"read") ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--shader-self-test") { return kt::gfx::ShaderSelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--dialog-self-test") {
        return DialogSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--rtc-self-test") {
        return RtcSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--libc-calendar-self-test") {
        return LibcCalendarSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--time-self-test") {
        return TimeSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--memory-self-test") {
        try { return MemorySelfTest() ? 0 : 1; }
        catch (const std::exception& error) {
            std::fprintf(stderr, "Native memory test error: %s\n", error.what());
            return 2;
        }
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--tracked-texture-self-test") {
        try { return kt::memory::TrackedTextureSelfTest() ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr, "Tracked texture self-test: %s\n", error.what()); return 2; }
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--write-tracker-self-test") {
        try { return kt::memory::WriteTrackerSelfTest() ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr, "Write tracker self-test: %s\n", error.what()); return 2; }
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--mspace-self-test") {
        return MspaceSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--video-self-test") {
        return VideoSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--display-layout-self-test") {
        return kt::gfx::DisplayLayoutSelfTest() ? 0 : 1;
    }
    if (argc == 2 && (std::wstring(argv[1]) == L"--graphics-self-test" || std::wstring(argv[1]) == L"--present-demo")) {
        try { return GraphicsSelfTest(std::wstring(argv[1]) == L"--present-demo") ? 0 : 1; }
        catch (const std::exception& error) {
            std::fprintf(stderr, "Native graphics self-test error: %s\n", error.what());
            return 2;
        }
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--cxx-guard-self-test") {
        return CxxGuardSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--equeue-self-test") {
        return EqueueSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--display-clock-self-test") {
        return DisplayClockSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--video-demo") {
        return VideoDemo();
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--pad-self-test") {
        return PadSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--pad-demo") {
        try { LoadKeyboardConfiguration(); }
        catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 2; }
        return PadDemo();
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--pad-demo" && std::wstring(argv[2]) == L"--input-config") {
        try { LoadKeyboardConfiguration(argv[3]); return PadDemo(); }
        catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 2; }
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--pad-demo" &&
        std::wstring(argv[2]) == L"--mouse-left-pad-button") {
        try { LoadKeyboardConfiguration(); }
        catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 2; }
        if (!ConfigureNativePadMouseLeft(argv[3])) { std::fputs("Invalid mouse pad button\n", stderr); return 2; }
        return PadDemo();
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--community-replay-check") {
        return kt::community::ReplayEnvelopeCheck(argv[2]) ? 0 : 2;
    }
    if (argc == 5 && std::wstring(argv[1]) == L"--community-replay-check") {
        try {
            const std::wstring board(argv[3]), score(argv[4]);
            const auto id = kt::community::Decimal(std::string(board.begin(), board.end()), 0xffffffff);
            const auto value = kt::community::Decimal(std::string(score.begin(), score.end()));
            return kt::community::ReplayEnvelopeCheck(argv[2], kt::community::ReplayExpectation{static_cast<std::uint32_t>(id), value}) ? 0 : 2;
        } catch (...) { std::fputs("Invalid replay metadata expectation\n", stderr); return 2; }
    }
    if (argc == 5 && std::wstring(argv[1]) == L"--community-replay-envelope-smoke") {
        try { return kt::community::ClientReplayEnvelopeSmoke(argv[2], argv[3], argv[4]) ? 0 : 1; }
        catch (...) { std::fputs("Community replay envelope smoke failed\n", stderr); return 1; }
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--community-config-check") {
        try {
            const auto config = kt::community::LoadConfig(argv[2]);
            std::printf("{\"event\":\"community_config_check\",\"enabled\":%s,\"valid\":true}\n", config.enabled ? "true" : "false");
            return 0;
        } catch (...) { std::fputs("Community config check failed\n", stderr); return 2; }
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--np-score-self-test") {
        try { return ScoreSelfTest() ? 0 : 1; }
        catch (...) { std::fputs("Native score self-test failed.\n", stderr); return 1; }
    }
    if (argc==2 && std::wstring_view(argv[1])==L"--np-score-profile-self-test") {
        try { return ScoreProfileSelfTest()?0:1; }
        catch (const std::exception& error) { std::fprintf(stderr,"Native score profile test: %s\n",error.what());return 1; }
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--community-round-audit-self-test") {
        try { return CommunityRoundAuditSelfTest() ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr,"Round audit self-test: %s\n",error.what()); return 2; }
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--native-round-self-test") {
        try { return NativeCommunityRoundsSelfTest(argv[2]) ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr,"Native round self-test: %s\n",error.what()); return 2; }
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--community-round-journal-self-test") {
        try { return kt::community::RoundJournalSelfTest(argv[2]) ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr,"Round journal self-test: %s\n",error.what()); return 2; }
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--community-round-journal-fixture") {
        try {
            const std::wstring phase(argv[3]);
            return kt::community::RoundJournalFixture(argv[2],std::string(phase.begin(),phase.end())) ? 0 : 1;
        } catch (const std::exception& error) { std::fprintf(stderr,"Round journal fixture: %s\n",error.what()); return 2; }
    }
    if (argc == 5 && std::wstring(argv[1]) == L"--community-round-journal-smoke") {
        try {
            const std::wstring phase(argv[4]);
            return kt::community::RoundJournalSmoke(argv[2],argv[3],std::string(phase.begin(),phase.end())) ? 0 : 1;
        } catch (const std::exception& error) { std::fprintf(stderr,"Round journal smoke: %s\n",error.what()); return 2; }
    }
    if (argc == 5 && std::wstring(argv[1]) == L"--community-round-client-smoke") {
        try {
            const std::wstring phase(argv[4]);
            return kt::community::ClientRoundSmoke(argv[2],argv[3],std::string(phase.begin(),phase.end())) ? 0 : 1;
        } catch (const std::exception& error) { std::fprintf(stderr,"Round client smoke: %s\n",error.what()); return 2; }
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--np-community-smoke") {
        try { return CommunityStateSmoke(argv[2], argv[3]) ? 0 : 1; }
        catch (...) { std::fputs("Native community state smoke failed.\n", stderr); return 1; }
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--np-replay-self-test") {
        try { return ScoreReplaySelfTest() ? 0 : 1; }
        catch (...) { std::fputs("Native replay self-test failed.\n", stderr); return 1; }
    }
    if (argc == 5 && std::wstring(argv[1]) == L"--np-replay-smoke") {
        try { return ScoreReplaySmoke(argv[2], argv[3], argv[4]) ? 0 : 1; }
        catch (...) { std::fputs("Native replay smoke failed.\n", stderr); return 1; }
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--np-score-smoke") {
        try { return ScoreSmoke(argv[2], argv[3]) ? 0 : 1; }
        catch (...) { std::fputs("Native score smoke failed.\n", stderr); return 1; }
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--community-client-self-test") {
        try { return kt::community::ClientSelfTest(argv[2]) ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr, "Community self-test: %s\n", error.what()); return 1; }
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--community-client-smoke") {
        try { return kt::community::ClientSmoke(argv[2], argv[3]) ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr, "Community smoke: %s\n", error.what()); return 1; }
    }
    if (argc==5 && std::wstring_view(argv[1])==L"--community-profile-smoke") {
        try { return kt::community::ClientSmoke(argv[2],argv[3],cave::Utf8(std::filesystem::path(argv[4])))?0:1; }
        catch (const std::exception& error) { std::fprintf(stderr,"Community profile smoke: %s\n",error.what());return 1; }
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--community-client-drain-smoke") {
        try { return kt::community::ClientDrainSmoke(argv[2], argv[3]) ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr, "Community drain: %s\n", error.what()); return 1; }
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--community-client-cancel-smoke") {
        try { return kt::community::ClientCancelSmoke(argv[2], argv[3]) ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr, "Community cancel: %s\n", error.what()); return 1; }
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--np-self-test") {
        return NpSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--scan-self-test") { return ScanSelfTest() ? 0 : 1; }
    if (argc == 2 && std::wstring(argv[1]) == L"--format-self-test") {
        return FormatSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--heap-self-test") {
        return GuestHeapSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--audio-self-test") {
        return AudioSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--ngs2-self-test") {
        return Ngs2SelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--thread-self-test") {
        return ThreadSelfTest() ? 0 : 1;
    }
    if (argc == 5 && std::wstring(argv[1]) == L"--content-self-test") {
        try { return ContentSelfTest(argv[2], argv[3], argv[4]) ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr, "Content self-test: %s\n", error.what()); return 2; }
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--file-self-test") {
        try { return FileSelfTest(argv[2], argv[3]) ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr, "File self-test: %s\n", error.what()); return 2; }
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--fios-self-test") {
        try { return FiosSelfTest(argv[2], argv[3]) ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr, "Fios self-test: %s\n", error.what()); return 2; }
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--pcm-voice-self-test") {
        return kt::audio::PcmVoiceSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--user-self-test") {
        return UserSelfTest() ? 0 : 1;
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--screenshot-self-test") {
        return ScreenshotSelfTest(std::filesystem::absolute(argv[2])) ? 0 : 1;
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--storage-self-test") {
        try { return StorageSelfTest(std::filesystem::absolute(argv[2])) ? 0 : 1; }
        catch (const std::exception& error) {
            std::fprintf(stderr, "Native storage test error: %s\n", error.what());
            return 2;
        }
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--save-store-self-test") {
        try { return SaveStoreSelfTest(std::filesystem::path(argv[2])) ? 0 : 1; }
        catch (const std::exception& error) { std::fprintf(stderr, "Native save-store self-test error: %s\n", error.what()); return 2; }
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--save-crash-fixture") {
        try {
            const std::wstring wide_phase(argv[3]);
            if (!std::all_of(wide_phase.begin(), wide_phase.end(), [](auto value) { return value > 0 && value < 128; })) { throw std::runtime_error("Invalid authored fixture phase"); }
            return SaveCrashFixture(std::filesystem::path(argv[2]), std::string(wide_phase.begin(), wide_phase.end())) ? 0 : 1;
        } catch (const std::exception& error) { std::fprintf(stderr, "Native save crash fixture error: %s\n", error.what()); return 2; }
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--dialog-demo") {
        return DialogDemo(argv[2]);
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--sync-self-test") {
        return MutexSelfTest() ? 0 : 1;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--abi-self-test") {
        using Function = std::uint64_t(KT_SYSV*)(std::uint64_t, std::uint64_t, std::uint64_t,
                                                std::uint64_t, std::uint64_t, std::uint64_t);
        volatile Function fixture = AbiFixture;
        const auto result = fixture(1, 2, 3, 4, 5, 6);
        std::printf("{\"event\":\"sysv_abi_self_test\",\"result\":%llu,\"expected\":91}\n", result);
        return result == 91 ? 0 : 1;
    }
    const wchar_t* user_directory = nullptr;
    const wchar_t* modules_path = nullptr;
    const wchar_t* app_path = nullptr;
    const wchar_t* patch_path = nullptr;
    const wchar_t* merged_app = nullptr;
    const wchar_t* addons_path = nullptr;
    const wchar_t* read_patches_path = nullptr;
    const wchar_t* mouse_left_button = nullptr;
    const wchar_t* texture_validation = nullptr;
    const wchar_t* attachment_validation = nullptr;
    const wchar_t* gpu_runtime = nullptr;
    const wchar_t* diagnostic_tap = nullptr;
    const wchar_t* diagnostic_circle_hold = nullptr;
    const wchar_t* input_config = nullptr;
    const wchar_t* collection_config = nullptr;
    const wchar_t* collection_game = nullptr;
    const wchar_t* allocation_timing = nullptr;
    const wchar_t* background_log = nullptr;
    const wchar_t* community_provider = nullptr;
    const wchar_t* community_round_audit = nullptr;
    const wchar_t* community_rounds = nullptr;
    bool valid_arguments = argc >= 3 && argc <= 41 && (argc % 2 == 1);
    for (int i = 3; valid_arguments && i + 1 < argc; i += 2) {
        if (std::wstring(argv[i]) == L"--user-dir" && !user_directory) { user_directory = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--module-dir" && !modules_path) { modules_path = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--app-dir" && !app_path) { app_path = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--patch-dir" && !patch_path) { patch_path = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--merged-app" && !merged_app) { merged_app = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--addon-dir" && !addons_path) { addons_path = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--read-patches" && !read_patches_path) { read_patches_path = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--mouse-left-pad-button" && !mouse_left_button) { mouse_left_button = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--texture-validation" && !texture_validation) { texture_validation = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--attachment-validation" && !attachment_validation) { attachment_validation = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--gpu-runtime" && !gpu_runtime) { gpu_runtime = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--diagnostic-tap-ms" && !diagnostic_tap) { diagnostic_tap = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--diagnostic-circle-hold-ms" && !diagnostic_circle_hold) { diagnostic_circle_hold = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--input-config" && !input_config) { input_config = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--collection-config" && !collection_config) { collection_config = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--collection-game" && !collection_game) { collection_game = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--allocation-timing" && !allocation_timing) { allocation_timing = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--background-log" && !background_log) { background_log = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--community-provider" && !community_provider) { community_provider = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--community-round-audit" && !community_round_audit) { community_round_audit = argv[i + 1]; }
        else if (std::wstring(argv[i]) == L"--community-rounds" && !community_rounds) { community_rounds = argv[i + 1]; }
        else { valid_arguments = false; }
    }
    if ((patch_path || addons_path) && !app_path) { valid_arguments = false; }
    if (merged_app && (!app_path || patch_path || std::wstring_view(merged_app)!=L"on" ||
        (std::wstring_view(argv[2])!=L"--enter" && std::wstring_view(argv[2])!=L"--validate"))) {valid_arguments=false;}
    if (read_patches_path && (!app_path || (std::wstring(argv[2]) != L"--enter" && std::wstring(argv[2]) != L"--validate"))) { valid_arguments = false; }
    if (mouse_left_button && std::wstring(argv[2]) != L"--enter") { valid_arguments = false; }
    if (gpu_runtime && std::wstring(argv[2]) != L"--enter") { valid_arguments = false; }
    if (diagnostic_tap && std::wstring(argv[2]) != L"--enter") { valid_arguments = false; }
    if (diagnostic_circle_hold && std::wstring(argv[2]) != L"--enter") { valid_arguments = false; }
    if (input_config && std::wstring(argv[2]) != L"--enter") { valid_arguments = false; }
    if (collection_config && (input_config || std::wstring(argv[2]) != L"--enter")) { valid_arguments = false; }
    if (collection_game && !collection_config) valid_arguments=false;
    const bool configured_community = community_provider && std::wstring(community_provider) == L"configured";
    if (community_provider && (std::wstring(argv[2]) != L"--enter" ||
        (!configured_community && std::wstring(community_provider) != L"loopback") || (!input_config && !collection_config) || !user_directory)) { valid_arguments = false; }
    if (configured_community && !community_rounds) { valid_arguments = false; }
    if (community_round_audit && (!community_provider || configured_community || std::wstring(community_round_audit) != L"on")) { valid_arguments = false; }
    if (community_rounds && (!community_provider || community_round_audit || std::wstring(community_rounds) != L"on")) { valid_arguments = false; }
    if (allocation_timing && ((std::wstring(argv[2]) != L"--enter" && std::wstring(argv[2]) != L"--validate") ||
        (std::wstring(allocation_timing) != L"full" && std::wstring(allocation_timing) != L"counts"))) { valid_arguments = false; }
    if (background_log && (std::wstring_view(argv[2])!=L"--enter" || std::wstring_view(background_log)!=L"on")) valid_arguments=false;
    if (texture_validation && (std::wstring(argv[2]) != L"--enter" ||
        (std::wstring(texture_validation) != L"full" && std::wstring(texture_validation) != L"tracked"))) { valid_arguments = false; }
    if (attachment_validation && (std::wstring(argv[2]) != L"--enter" ||
        (std::wstring(attachment_validation) != L"upload" && std::wstring(attachment_validation) != L"reuse"))) { valid_arguments = false; }
    if (valid_arguments && std::wstring(argv[2]) == L"--trace-enter") {
        return TraceNativeProcess(argv[0], argv[1], user_directory, modules_path, app_path, patch_path, addons_path);
    }
    const bool census = argc >= 3 && std::wstring(argv[2]) == L"--binding-census";
    if (census && (user_directory || app_path || patch_path || addons_path)) { valid_arguments = false; }
    if (!valid_arguments || (std::wstring(argv[2]) != L"--validate" && std::wstring(argv[2]) != L"--enter" && !census)) {
        std::fputs("Allocation diagnostics: --allocation-timing full|counts is accepted with --enter or --validate.\nCollection configuration: --enter accepts --collection-config config.ini instead of the legacy --input-config. Check without execution: --collection-config-check config.ini.\nMerged data: --app-dir path --merged-app on explicitly selects a complete merged patch layout, without --patch-dir.\n", stderr);
        std::fputs("Usage: KetsuiNativeProbe input.ktprobe --validate|--enter|--trace-enter [--user-dir absolute-path] [--module-dir absolute-path] [--app-dir absolute-path] [--patch-dir absolute-path] [--addon-dir absolute-path]\n       --enter also accepts --input-config path, --mouse-left-pad-button none|cross|circle|square|triangle|options|touchpad, --texture-validation full|tracked, --attachment-validation upload|reuse\n       KetsuiNativeProbe --input-config-check input.ini\n       KetsuiNativeProbe input.ktprobe --binding-census [--module-dir absolute-path]\n", stderr);
        return 2;
    }
    try {
        struct BackgroundLogScope {
            bool owns{};
            ~BackgroundLogScope() {if (owns) {(void)StopNativeAsyncLog();PrintNativeAsyncLogDiagnostics();}}
        } background;
        if (background_log) {
            background.owns=StartNativeAsyncLog();
            if (!background.owns) throw std::runtime_error("Background diagnostic log already started");
        }
        if (allocation_timing && !ConfigureNativeAllocationTiming(std::wstring(allocation_timing) == L"full")) {
            throw std::runtime_error("Allocation timing must be configured before execution");
        }
        std::optional<cave::ps4::CollectionConfiguration> collection;
        if (collection_config) {
            collection=cave::ps4::LoadCollectionConfiguration(collection_config,
                collection_game ? cave::Utf8(std::filesystem::path(collection_game)) : "ketsui");
            cave::ps4::ApplyCollectionConfiguration(*collection);
        } else if (std::wstring(argv[2]) == L"--enter") { LoadKeyboardConfiguration(input_config); }
        if (community_provider) {
            if (collection && collection->game!="ketsui") throw std::runtime_error("This title's community adapter is not yet implemented");
            const auto config = collection ? collection->community : kt::community::LoadConfig(input_config);
            const auto endpoint = kt::community::ParseEndpoint(config.endpoint);
            if (!std::filesystem::path(user_directory).is_absolute()) throw std::runtime_error("Community provider requires an absolute user directory");
            if (configured_community) {
                if (config.enabled && !endpoint.secure) throw std::runtime_error("Configured community service requires HTTPS");
            } else if (!config.enabled || endpoint.secure || endpoint.host != L"127.0.0.1") {
                throw std::runtime_error("Community development entry requires enabled literal HTTP loopback");
            }
            if (config.enabled) {
                auto instance=std::make_shared<kt::community::Client>(config,std::filesystem::path(user_directory),community_rounds!=nullptr);
                if (community_rounds && !ConfigureNativeCommunityRounds(instance)) throw std::runtime_error("Community round producer already configured");
                if (!ConfigureNativeCommunity(instance)) throw std::runtime_error("Community provider already configured");
                if (community_round_audit && !ConfigureNativeCommunityRoundAudit()) throw std::runtime_error("Community round audit already configured");
            }
            std::printf("{\"event\":\"native_community_configuration\",\"configured\":%s,\"enabled\":%s}\n",
                        configured_community?"true":"false",config.enabled?"true":"false");
        }
        if (diagnostic_circle_hold) {
            const std::wstring value(diagnostic_circle_hold);
            if (value.empty() || value.size()>4 || value.find_first_not_of(L"0123456789")!=std::wstring::npos ||
                !ConfigureNativeDiagnosticCircleHold(static_cast<unsigned>(std::stoul(value)))) { throw std::runtime_error("Invalid diagnostic Circle hold (0..2000 ms), or H is already bound in the input config"); }
        }
        if (diagnostic_tap) {
            const std::wstring value(diagnostic_tap);
            if (value.empty() || value.size() > 3 || value.find_first_not_of(L"0123456789") != std::wstring::npos ||
                !ConfigureNativeDiagnosticTaps(static_cast<unsigned>(std::stoul(value)))) { throw std::runtime_error("Invalid diagnostic tap duration (0..500 ms)"); }
        }
        if (mouse_left_button && !ConfigureNativePadMouseLeft(mouse_left_button)) { throw std::runtime_error("Invalid mouse pad button"); }
        if (texture_validation) { ConfigureNativeTextureTracking(std::wstring(texture_validation) == L"tracked"); }
        if (attachment_validation) { ConfigureNativeAttachmentReuse(std::wstring(attachment_validation) == L"reuse"); }
        if (app_path) { ConfigureNativeContent(app_path, patch_path ? patch_path : L"", addons_path ? addons_path : L"", user_directory ? user_directory : L"", read_patches_path ? read_patches_path : L"",merged_app!=nullptr); }
        if (user_directory) { ConfigureNativeStorage(user_directory,app_path ? NativeContentTitleId() : "CUSA09062"); }
        if (modules_path) {
            module_directory = modules_path;
            if (!module_directory.is_absolute() || !std::filesystem::is_directory(module_directory)) { throw std::runtime_error("Module directory must be an existing absolute path"); }
            CheckModulePath(module_directory);
        }
        InitializeNativeTime();
        const auto resident_gpu = gpu_runtime ? std::make_unique<NativeResidentGpu>(gpu_runtime) : nullptr;
        Runtime runtime;
        active_runtime = &runtime;
        SetUnhandledExceptionFilter(Unhandled);
        AddVectoredExceptionHandler(0, ProbeFault);
        Reader reader(argv[1]);
        Load(runtime, reader);
        if (census) {
            CensusBindings(runtime);
            return 0;
        }
        if (std::wstring(argv[2]) == L"--validate") {
            std::puts("{\"event\":\"validation_complete\",\"game_entry_reached\":false}");
            return 0;
        }
        struct Parameters {
            std::uint64_t argc;
            const char* argv[33];
        } parameters{1, {"eboot.bin"}};
        using Entry = void(KT_SYSV*)(Parameters*, void(KT_SYSV*)());
        const auto entry = reinterpret_cast<Entry>(runtime.image + runtime.header.entry);
        std::puts("{\"event\":\"enter_game\",\"runtime\":\"KetsuiNativeProbe\",\"graphics\":false}");
        runtime.entered = true;
        entry(&parameters, ProgramExit);
        throw std::runtime_error("Game process entry unexpectedly returned");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Native probe error: %s\n", error.what());
        return 2;
    }
}

int wmain(int argc,wchar_t** argv) {
    return cave::ps4::IsCollectionLaunch(argc,argv) ? cave::ps4::RunCollection(argc,argv,NativeCommand) : NativeCommand(argc,argv);
}
