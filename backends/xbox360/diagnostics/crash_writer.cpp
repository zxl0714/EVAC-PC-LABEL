#include "crash_shared.h"
#include <dbghelp.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string UTF8(const wchar_t* value) {
  int n = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return {};
  std::string result(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), n, nullptr, nullptr);
  result.resize(n - 1);
  return result;
}
void SaveText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream file(path, std::ios::binary);
  file << "\xEF\xBB\xBF" << text;
}
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) return 2;
  const DWORD pid = wcstoul(argv[1], nullptr, 10);
  const std::wstring stem = argv[2];
  HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, (stem + L"-memory").c_str());
  HANDLE request = OpenEventW(SYNCHRONIZE, FALSE, (stem + L"-request").c_str());
  HANDLE done = OpenEventW(EVENT_MODIFY_STATE, FALSE, (stem + L"-done").c_str());
  HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, (stem + L"-ready").c_str());
  HANDLE game = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  if (!mapping || !request || !done || !ready || !game) return 3;
  auto shared = static_cast<const CrashRecord*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(CrashRecord)));
  if (!shared) return 4;
  SetEvent(ready);
  HANDLE waits[]{request, game};
  if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0) {
    CrashRecord snapshot{};
    std::memcpy(&snapshot, shared, sizeof(snapshot));
    const std::wstring prefix = snapshot.report_prefix;
    std::ostringstream summary;
    summary << "BUILD=" << snapshot.build << "\r\nGAME=" << snapshot.game << "\r\nSCHEMA=2\r\nPID=" << pid
            << "\r\nTHREAD=" << snapshot.thread_id << "\r\nREASON=" << snapshot.reason
            << "\r\nLAST_GUEST_MODULE=" << snapshot.module_path
            << "\r\nMODULE_PHASE=" << snapshot.phase << "\r\n";
    summary << std::hex << std::uppercase << "EXCEPTION_CODE=0x" << snapshot.exception.ExceptionCode
            << "\r\nEXCEPTION_ADDRESS=0x" << reinterpret_cast<uint64_t>(snapshot.exception.ExceptionAddress)
            << "\r\nRIP=0x" << snapshot.context.Rip << "\r\nRSP=0x" << snapshot.context.Rsp << "\r\n";
    MEMORY_BASIC_INFORMATION info{};
    wchar_t native_module[4096]{};
    if (VirtualQueryEx(game, reinterpret_cast<void*>(snapshot.context.Rip), &info, sizeof(info)) &&
        GetMappedFileNameW(game, info.AllocationBase, native_module, 4096)) {
      summary << "FAULT_HOST_MODULE=" << UTF8(native_module) << "\r\nFAULT_HOST_BASE=0x"
              << reinterpret_cast<uint64_t>(info.AllocationBase) << "\r\nFAULT_HOST_RVA=0x"
              << snapshot.context.Rip - reinterpret_cast<uint64_t>(info.AllocationBase) << "\r\n";
    }
    summary << "\r\nSIGABRT uses a synthetic signal-handler context; use game.log for the original fatal guest address.\r\n";
    const auto summary_path = std::filesystem::path(prefix + L"-crash.txt");
    SaveText(summary_path, summary.str()); // Preserve location even if dump generation fails.
    HANDLE dump = CreateFileW((prefix + L"-crash.dmp").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool dump_ok = false;
    DWORD dump_error = ERROR_SUCCESS;
    if (dump != INVALID_HANDLE_VALUE) {
      EXCEPTION_POINTERS pointers{&snapshot.exception, &snapshot.context};
      MINIDUMP_EXCEPTION_INFORMATION exception_info{snapshot.thread_id, &pointers, FALSE};
      auto flags = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo |
                                             MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory);
      dump_ok = MiniDumpWriteDump(game, pid, dump, flags, &exception_info, nullptr, nullptr) != FALSE;
      if (!dump_ok) dump_error = GetLastError();
      CloseHandle(dump);
    } else { dump_error = GetLastError(); }
    summary << "DUMP_OK=" << dump_ok << "\r\nDUMP_ERROR=0x" << dump_error << "\r\n";
    // Capture only the active guest module, on crash, never the entire 4 GB guest
    // address space. This replaces repeated manual module/table capture sessions.
    if (snapshot.guest_memory_base && snapshot.guest_image_base &&
        snapshot.guest_image_size && snapshot.guest_image_size <= 32 * 1024 * 1024) {
      std::vector<char> image(snapshot.guest_image_size, 0);
      size_t missing = 0;
      for (size_t offset = 0; offset < image.size(); offset += 4096) {
        size_t bytes = (std::min)(size_t{4096}, image.size() - offset);
        SIZE_T read = 0;
        auto address = reinterpret_cast<void*>(snapshot.guest_memory_base + snapshot.guest_image_base + offset);
        if (!ReadProcessMemory(game, address, image.data() + offset, bytes, &read) || read != bytes) {
          std::memset(image.data() + offset, 0, bytes);
          summary << "MISSING_GUEST_PAGE=0x" << snapshot.guest_image_base + offset << "\r\n";
          missing++;
        }
      }
      std::ofstream file(std::filesystem::path(prefix + L"-module.bin"), std::ios::binary);
      file.write(image.data(), static_cast<std::streamsize>(image.size()));
      summary << std::dec << "MODULE_SNAPSHOT_BYTES=" << image.size()
              << "\r\nMODULE_MISSING_PAGES=" << missing << "\r\nMODULE_SNAPSHOT_OK=" << file.good() << "\r\n";
    }
    SaveText(summary_path, summary.str());
    SetEvent(done);
  }
  UnmapViewOfFile(shared);
  for (HANDLE handle : {mapping, request, done, ready, game}) CloseHandle(handle);
  return 0;
}
