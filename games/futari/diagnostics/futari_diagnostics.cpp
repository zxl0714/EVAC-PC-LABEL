#include "diagnostics_shared.h"
#include "futari_diagnostics.h"
#include <rex/logging.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace {
FutariCrashRecord* record = nullptr;
HANDLE request_event = nullptr;
HANDLE done_event = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;

void RecordCrash(EXCEPTION_POINTERS* pointers, const char* reason) {
  if (!record || InterlockedCompareExchange(&record->claimed, 1, 0) != 0) return;
  record->thread_id = GetCurrentThreadId();
  record->exception = *pointers->ExceptionRecord;
  record->exception.ExceptionRecord = nullptr;
  record->context = *pointers->ContextRecord;
  strcpy_s(record->reason, reason);
  MemoryBarrier();
  SetEvent(request_event);
  // The separate writer remains alive while this thread waits. A broken writer
  // must never prevent termination indefinitely, or attempt to resume the game.
  WaitForSingleObject(done_event, 20000);
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* pointers) {
  RecordCrash(pointers, "unhandled-exception");
  if (previous_filter) return previous_filter(pointers);
  return EXCEPTION_EXECUTE_HANDLER;
}

void AbortHandler(int) {
  CONTEXT context{};
  RtlCaptureContext(&context);
  EXCEPTION_RECORD exception{};
  exception.ExceptionCode = 0x40000015; // STATUS_FATAL_APP_EXIT, synthetic SIGABRT context.
  exception.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
  EXCEPTION_POINTERS pointers{&exception, &context};
  RecordCrash(&pointers, "SIGABRT-synthetic-context");
  // abort() will terminate after this handler returns.
}
}

void FutariDiagnosticsModule(const char* path, const char* phase,
                            uint64_t memory_base, uint32_t image_base,
                            uint32_t image_size) {
  if (!record) return;
  strncpy_s(record->module_path, path ? path : "", _TRUNCATE);
  strncpy_s(record->phase, phase ? phase : "", _TRUNCATE);
  record->guest_memory_base = memory_base;
  record->guest_image_base = image_base;
  record->guest_image_size = image_size;
}

void FutariInitDiagnostics() {
  REXLOG_INFO("Futari build: {}; diagnostics schema=1; no per-frame capture", kFutariBuild);
  wchar_t exe[4096]{};
  if (!GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)))) return;
  auto directory = std::filesystem::path(exe).parent_path();
  wchar_t prefix[4096]{};
  auto length = GetEnvironmentVariableW(L"FUTARI_REPORT_PREFIX", prefix,
                                        static_cast<DWORD>(std::size(prefix)));
  if (!length) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t run[128]{};
    swprintf_s(run, L"%04u%02u%02u-%02u%02u%02u-%03u-%lu",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
               now.wSecond, now.wMilliseconds, GetCurrentProcessId());
    auto fallback = directory / L"logs" / run;
    wcsncpy_s(prefix, fallback.c_str(), _TRUNCATE);
  } else if (length >= std::size(prefix)) {
    REXLOG_WARN("Futari crash reporter disabled: output path is too long");
    return;
  }
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(prefix).parent_path(), error);
  if (error) {
    REXLOG_WARN("Futari crash reporter disabled: cannot create output directory ({})", error.value());
    return;
  }
  wchar_t stem[128]{};
  swprintf_s(stem, L"Local\\FutariDiag-%lu-%llu", GetCurrentProcessId(), GetTickCount64());
  const auto mapping_name = std::wstring(stem) + L"-memory";
  const auto request_name = std::wstring(stem) + L"-request";
  const auto done_name = std::wstring(stem) + L"-done";
  const auto ready_name = std::wstring(stem) + L"-ready";
  HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, sizeof(FutariCrashRecord), mapping_name.c_str());
  if (!mapping) { REXLOG_WARN("Futari crash mapping failed: {}", GetLastError()); return; }
  auto shared = static_cast<FutariCrashRecord*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FutariCrashRecord)));
  request_event = CreateEventW(nullptr, TRUE, FALSE, request_name.c_str());
  done_event = CreateEventW(nullptr, TRUE, FALSE, done_name.c_str());
  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str());
  if (!shared || !request_event || !done_event || !ready) {
    REXLOG_WARN("Futari crash event setup failed: {}", GetLastError());
    if (shared) UnmapViewOfFile(shared);
    if (request_event) CloseHandle(request_event);
    if (done_event) CloseHandle(done_event);
    if (ready) CloseHandle(ready);
    CloseHandle(mapping);
    return;
  }
  std::memset(shared, 0, sizeof(*shared));
  wcscpy_s(shared->report_prefix, prefix);
  strcpy_s(shared->phase, "startup");
  const auto helper = directory / L"futari_crash_writer.exe";
  std::wstring command = L"\"" + helper.wstring() + L"\" " +
                         std::to_wstring(GetCurrentProcessId()) + L" \"" + stem + L"\"";
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  const BOOL started = CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE,
                                     CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process);
  if (started) {
    CloseHandle(process.hThread);
    HANDLE waits[]{ready, process.hProcess};
    const auto ready_status = WaitForMultipleObjects(2, waits, FALSE, 5000);
    CloseHandle(process.hProcess);
    if (ready_status == WAIT_OBJECT_0) record = shared;
  }
  CloseHandle(ready);
  if (!record) {
    REXLOG_WARN("Futari crash writer unavailable; ordinary logs remain enabled (start={})", static_cast<bool>(started));
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    CloseHandle(request_event);
    CloseHandle(done_event);
    request_event = done_event = nullptr;
    return;
  }
  CloseHandle(mapping); // Both mapped views retain the mapping until process exit.
  previous_filter = SetUnhandledExceptionFilter(CrashFilter);
  std::signal(SIGABRT, AbortHandler);
  REXLOG_INFO("Futari crash writer ready: pid={}; logs/dump/module snapshot only on crash", GetCurrentProcessId());
  // Explicit local testing only. Not used by the portable launch scripts.
  wchar_t selftest[32]{};
  if (GetEnvironmentVariableW(L"FUTARI_DIAGNOSTICS_SELFTEST", selftest, 32)) {
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    if (wcscmp(selftest, L"seh") == 0) RaiseException(0xE042F001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    if (wcscmp(selftest, L"abort") == 0) std::abort();
  }
}
