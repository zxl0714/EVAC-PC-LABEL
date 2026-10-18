#include "futari_init.h"
#include "common/catalogue.h"
#include <rex/filesystem.h>
#include "futari_diagnostics.h"
#include "diagnostics_shared.h"

#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_module.h>
#include <rex/system/xmodule.h>
#include <rex/system/user_module.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <string>

namespace {
struct PendingUnload {
  std::string path;
  std::string key;
  uint32_t handle = 0;
  uint16_t load_count = 0;
};
thread_local PendingUnload pending;
thread_local std::string requested_module;
thread_local uint32_t redirected_name = 0;
constexpr uint32_t kSharedCodeBase = 0x88070000;
constexpr const char* kModuleNames[] = {
    "hirow_abnormal", "hirow_normal", "kiniro_abnormal", "kiniro_normal",
    "palm_abnormal", "palm_normal", "reco_abnormal", "reco_normal"};
constexpr const char* kDeviceModuleRoot = "\\Device\\Harddisk0\\Partition1\\media\\Module\\";
}

void FutariRegisterModuleAliases() {
  // UserModule stores its VFS-resolved device path. Use that same registry key
  // on load so the SDK can find and tear down its dispatcher and DLL on unload.
  auto* kernel = rex::Runtime::instance()->kernel_state();
  for (const char* name : kModuleNames) {
    const std::string file = std::string(name) + ".xex";
    const std::string guest = std::string(kDeviceModuleRoot) + file;
    const auto folder = rex::filesystem::GetExecutableFolder().parent_path() / "games/futari";
    const std::string library = cave::Utf8(folder / (std::string("futari_") + name + ".dll"));
    kernel->RegisterRecompiledModule(file.c_str(), guest.c_str(), library.c_str());
  }
  wchar_t selftest[32]{};
  if (GetEnvironmentVariableW(L"FUTARI_DIAGNOSTICS_SELFTEST", selftest, 32) &&
      wcscmp(selftest, L"module") == 0) {
    auto module = kernel->LoadUserModule(std::string(kDeviceModuleRoot) + "palm_normal.xex", false);
    if (!module) REX_FATAL("Futari module snapshot selftest: load failed");
    auto* runtime = rex::Runtime::instance();
    auto* ldr = runtime->memory()->TranslateVirtual<rex::system::X_LDR_DATA_TABLE_ENTRY*>(module->hmodule_ptr());
    FutariDiagnosticsModule(module->path().c_str(), "loaded-selftest",
        reinterpret_cast<uint64_t>(runtime->memory()->virtual_membase()), ldr->image_base, ldr->full_image_size);
    REXLOG_INFO("Futari module snapshot selftest: base={:08X} size={:08X}",
                uint32_t(ldr->image_base), uint32_t(ldr->full_image_size));
    RaiseException(0xE042F002, EXCEPTION_NONCONTINUABLE, 0, nullptr);
  }
}

void FutariBeforeUnload(PPCRegister& handle) {
  pending = {};
  auto* runtime = rex::Runtime::instance();
  auto* kernel = runtime->kernel_state();
  auto* ldr = runtime->memory()->TranslateVirtual<rex::system::X_LDR_DATA_TABLE_ENTRY*>(handle.u32);
  auto module = rex::system::XModule::GetFromHModule(kernel, ldr);
  if (!module) return;
  pending.path = module->path();
  pending.handle = handle.u32;
  pending.load_count = ldr->load_count;
  if (auto recomp = kernel->FindRecompiledModule(pending.path)) pending.key = recomp->guest_path;
  REXLOG_INFO("Futari unload begin: path='{}' hmodule={:08X} load_count={}",
              pending.path, pending.handle, pending.load_count);
  FutariDiagnosticsModule(pending.path.c_str(), "unloading",
      reinterpret_cast<uint64_t>(runtime->memory()->virtual_membase()), ldr->image_base, ldr->full_image_size);
}

void FutariAfterUnload(PPCRegister& status) {
  if (pending.path.empty()) return;
  auto* runtime = rex::Runtime::instance();
  auto* kernel = runtime->kernel_state();
  const auto old = pending;
  pending = {};
  auto remaining = kernel->GetModule(old.path, true);
  REXLOG_INFO("Futari unload end: path='{}' status={:08X} still_loaded={}",
              old.path, status.u32, static_cast<bool>(remaining));
  if (status.u32 == 0 && !remaining) FutariDiagnosticsModule(old.path.c_str(), "unloaded");
  // Only remove a stale dispatcher table after successful final guest unload.
  // A module that is still loaded, or a failed unload, must retain its table.
  if (status.u32 != 0 || remaining || old.key.empty()) return;
  auto* dispatcher = runtime->function_dispatcher();
  if (dispatcher->FindCallerModuleBase(kSharedCodeBase) != kSharedCodeBase) return;
  auto range = dispatcher->UnregisterModule(old.key);
  if (range) {
    for (const char* name : {"xboxkrnl.exe", "xam.xex"}) {
      auto km = kernel->GetKernelModule(name);
      if (km) km->InvalidateThunkCacheInRange(range->first, range->second);
    }
    REXLOG_INFO("Futari removed stale dispatcher table after unload: '{}'", old.key);
  }
}

void FutariBeforeLoad(PPCRegister& name) {
  auto* runtime = rex::Runtime::instance();
  requested_module = runtime->memory()->TranslateVirtual<const char*>(name.u32);
  FutariDiagnosticsModule(requested_module.c_str(), "loading");
  auto basename = requested_module.substr(requested_module.find_last_of("/\\") + 1);
  std::transform(basename.begin(), basename.end(), basename.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const char* module_name : kModuleNames) {
    if (basename != std::string(module_name) + ".xex") continue;
    const std::string physical = std::string(kDeviceModuleRoot) + basename;
    redirected_name = runtime->memory()->SystemHeapAlloc(static_cast<uint32_t>(physical.size() + 1));
    if (!redirected_name) REX_FATAL("Futari: unable to allocate module load path");
    std::memcpy(runtime->memory()->TranslateVirtual<void*>(redirected_name),
                physical.c_str(), physical.size() + 1);
    name.u32 = redirected_name;
    REXLOG_INFO("Futari canonical module load: '{}' -> '{}'", requested_module, physical);
    break;
  }
  REXLOG_INFO("Futari load begin: '{}' shared_table_base={:08X}", requested_module,
              runtime->function_dispatcher()->FindCallerModuleBase(kSharedCodeBase));
}

void FutariAfterLoad(PPCRegister& status) {
  REXLOG_INFO("Futari load end: '{}' status={:08X}", requested_module, status.u32);
  if (status.u32 == 0) {
    auto* runtime = rex::Runtime::instance();
    const auto basename = requested_module.substr(requested_module.find_last_of("/\\") + 1);
    auto module = runtime->kernel_state()->GetModule(std::string(kDeviceModuleRoot) + basename, true);
    if (module) {
      auto* ldr = runtime->memory()->TranslateVirtual<rex::system::X_LDR_DATA_TABLE_ENTRY*>(module->hmodule_ptr());
      FutariDiagnosticsModule(module->path().c_str(), "loaded",
          reinterpret_cast<uint64_t>(runtime->memory()->virtual_membase()), ldr->image_base, ldr->full_image_size);
      REXLOG_INFO("Futari diagnostic module: '{}' guest_base={:08X} image_size={:08X}",
                  module->path(), uint32_t(ldr->image_base), uint32_t(ldr->full_image_size));
    }
  }
  if (redirected_name) {
    rex::Runtime::instance()->memory()->SystemHeapFree(redirected_name);
    redirected_name = 0;
  }
  requested_module.clear();
}
