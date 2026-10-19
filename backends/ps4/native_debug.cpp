#include "native_debug.h"

#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {
bool Context(HANDLE process, HANDLE thread, const char* reason) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_ALL;
    if (!GetThreadContext(thread, &context)) {
        std::fprintf(stderr, "debug_context_failed error=%lu\n", GetLastError());
        return false;
    }
    std::fprintf(stderr, "debug_context reason=%s rip=0x%llx rsp=0x%llx rbp=0x%llx "
                 "rax=0x%llx rcx=0x%llx rdx=0x%llx rbx=0x%llx rsi=0x%llx rdi=0x%llx "
                 "r8=0x%llx r9=0x%llx r10=0x%llx r11=0x%llx r12=0x%llx r13=0x%llx "
                 "r14=0x%llx r15=0x%llx flags=0x%lx\n", reason,
                 context.Rip, context.Rsp, context.Rbp, context.Rax, context.Rcx, context.Rdx,
                 context.Rbx, context.Rsi, context.Rdi, context.R8, context.R9, context.R10,
                 context.R11, context.R12, context.R13, context.R14, context.R15, context.EFlags);
    std::array<unsigned char, 32> code{};
    SIZE_T read{};
    if (ReadProcessMemory(process, reinterpret_cast<void*>(context.Rip), code.data(), code.size(), &read)) {
        std::fputs("debug_instruction_bytes=", stderr);
        for (const auto byte : code) {
            std::fprintf(stderr, "%02x", byte);
        }
        std::fputc('\n', stderr);
    }
    std::array<std::uint64_t, 128> stack{};
    if (ReadProcessMemory(process, reinterpret_cast<void*>(context.Rsp), stack.data(), sizeof(stack), &read)) {
        std::fputs("debug_stack_qwords=", stderr);
        for (const auto word : stack) {
            std::fprintf(stderr, " %llx", word);
        }
        std::fputc('\n', stderr);
    }
    std::fflush(stderr);
    return true;
}

void Module(HANDLE file, const void* base) {
    wchar_t path[32768]{};
    const auto size = GetFinalPathNameByHandleW(file, path, static_cast<DWORD>(std::size(path)), 0);
    std::fprintf(stderr, "debug_module base=%p path=%ls\n", base,
                 size && size < std::size(path) ? path : L"(unknown)");
    if (file && file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
}
} // namespace

int SampleNativeProcess(unsigned process_id) {
    if (!process_id || process_id == GetCurrentProcessId()) { return 2; }
    struct Handle {
        HANDLE value{};
        ~Handle() { if (value && value != INVALID_HANDLE_VALUE) { CloseHandle(value); } }
    } process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, process_id)};
    if (!process.value) { std::fprintf(stderr, "debug_sample_open_failed error=%lu\n", GetLastError()); return 2; }
    wchar_t path[32768]{}; DWORD length = static_cast<DWORD>(std::size(path));
    if (!QueryFullProcessImageNameW(process.value, 0, path, &length)) { return 2; }
    const std::wstring full_path(path, length); const auto separator = full_path.find_last_of(L"\\/");
    if (_wcsicmp(full_path.c_str() + (separator == std::wstring::npos ? 0 : separator + 1), L"KetsuiNativeProbe.exe")) {
        std::fputs("debug_sample_target_is_not_native_probe\n", stderr); return 2;
    }
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)};
    if (snapshot.value == INVALID_HANDLE_VALUE) { return 2; }
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry); unsigned sampled{}, failed{};
    if (!Thread32First(snapshot.value, &entry)) { return 2; }
    do {
        if (entry.th32OwnerProcessID != process_id) { continue; }
        Handle thread{OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID)};
        if (!thread.value || GetProcessIdOfThread(thread.value) != process_id) { ++failed; continue; }
        const auto previous = SuspendThread(thread.value);
        if (previous == static_cast<DWORD>(-1)) { ++failed; continue; }
        struct Resume {
            HANDLE thread; unsigned& failed;
            ~Resume() {
                if (ResumeThread(thread) == static_cast<DWORD>(-1)) {
                    std::fprintf(stderr, "debug_sample_resume_failed error=%lu\n", GetLastError()); ++failed;
                }
            }
        } resume{thread.value, failed};
        std::fprintf(stderr, "debug_sample_thread process=%u native_id=%lu previous_suspend_count=%lu\n", process_id, entry.th32ThreadID, previous);
        if (Context(process.value, thread.value, "manual_sample")) { ++sampled; } else { ++failed; }
    } while (Thread32Next(snapshot.value, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) { ++failed; std::fputs("debug_sample_enumeration_failed\n", stderr); }
    std::fprintf(stderr, "debug_sample_completed process=%u sampled=%u failed=%u\n", process_id, sampled, failed);
    return sampled && !failed ? 0 : 2;
}

int TraceNativeProcess(const wchar_t* executable, const wchar_t* input, const wchar_t* user_directory, const wchar_t* module_directory,
                       const wchar_t* app_directory, const wchar_t* patch_directory, const wchar_t* addons_directory) {
    std::wstring command = L"\"" + std::wstring(executable) + L"\" \"" + input + L"\" --enter";
    if (user_directory) {
        std::wstring path(user_directory);
        // CRT command-line parsing doubles trailing backslashes before a quote.
        const auto last = path.find_last_not_of(L'\\');
        if (last == std::wstring::npos) { return 2; }
        path.append(path.size() - last - 1, L'\\');
        command += L" --user-dir \"" + path + L"\"";
    }
    if (module_directory) {
        std::wstring path(module_directory);
        const auto last = path.find_last_not_of(L'\\');
        if (last == std::wstring::npos) { return 2; }
        path.append(path.size() - last - 1, L'\\');
        command += L" --module-dir \"" + path + L"\"";
    }
    const wchar_t* directories[]{app_directory, patch_directory, addons_directory};
    const wchar_t* options[]{L"--app-dir", L"--patch-dir", L"--addon-dir"};
    for (unsigned i = 0; i < 3; ++i) {
        if (!directories[i]) { continue; }
        std::wstring path(directories[i]);
        if (path.find(L'\"') != std::wstring::npos) { return 2; }
        const auto last = path.find_last_not_of(L'\\');
        if (last == std::wstring::npos) { return 2; }
        path.append(path.size() - last - 1, L'\\');
        command += L" " + std::wstring(options[i]) + L" \"" + path + L"\"";
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE,
                        DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "debug_launch_failed error=%lu\n", GetLastError());
        return 2;
    }
    bool initial_break = true;
    bool timed_out = false;
    bool exited = false;
    int status = 2;
    const auto started = GetTickCount64();
    while (!exited) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 250)) {
            if (GetLastError() != ERROR_SEM_TIMEOUT) {
                std::fprintf(stderr, "debug_wait_failed error=%lu\n", GetLastError());
                break;
            }
            if (GetTickCount64() - started >= 5000) {
                // Suspend only our child thread. The regular run wrapper also
                // has an outer timeout; this path leaves no attached survivor.
                if (SuspendThread(process.hThread) != static_cast<DWORD>(-1)) {
                    Context(process.hProcess, process.hThread, "watchdog");
                    ResumeThread(process.hThread);
                }
                TerminateProcess(process.hProcess, 26);
                timed_out = true;
            }
            if (GetTickCount64() - started >= 7000) {
                break;
            }
            continue;
        }
        DWORD continuation = DBG_CONTINUE;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            Module(event.u.CreateProcessInfo.hFile, event.u.CreateProcessInfo.lpBaseOfImage);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            // The debugging API owns thread handles until their exit event.
            break;
        case LOAD_DLL_DEBUG_EVENT:
            Module(event.u.LoadDll.hFile, event.u.LoadDll.lpBaseOfDll);
            break;
        case EXCEPTION_DEBUG_EVENT: {
            const auto& exception = event.u.Exception;
            const auto code = exception.ExceptionRecord.ExceptionCode;
            if (code == EXCEPTION_BREAKPOINT && initial_break) {
                initial_break = false;
                break;
            }
            std::fprintf(stderr, "debug_exception code=0x%lx first_chance=%lu address=%p\n", code,
                         exception.dwFirstChance, exception.ExceptionRecord.ExceptionAddress);
            HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, event.dwThreadId);
            if (thread) {
                Context(process.hProcess, thread, "exception");
                CloseHandle(thread);
            }
            continuation = DBG_EXCEPTION_NOT_HANDLED;
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            status = static_cast<int>(event.u.ExitProcess.dwExitCode);
            exited = true;
            break;
        default:
            break;
        }
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continuation)) {
            std::fprintf(stderr, "debug_continue_failed error=%lu\n", GetLastError());
            break;
        }
    }
    if (!exited) {
        TerminateProcess(process.hProcess, 26);
    }
    WaitForSingleObject(process.hProcess, 1000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::fprintf(stderr, "debug_finished status=%d timed_out=%s\n", status, timed_out ? "true" : "false");
    return timed_out ? 26 : status;
}
