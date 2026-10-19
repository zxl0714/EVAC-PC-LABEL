#include "cache_memory.h"
#include <Windows.h>
#include <atomic>
#include <stdexcept>

namespace Ketsui::GpuCore {
namespace {
std::atomic<CacheMemoryBackend *> backend{};
std::atomic<CacheFaultHandler> fault_handler{};
LONG CALLBACK CacheException(EXCEPTION_POINTERS *exception) {
  if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      exception->ExceptionRecord->NumberParameters < 2 ||
      exception->ExceptionRecord->ExceptionInformation[0] > 1) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  auto *memory = backend.load(std::memory_order_acquire);
  const auto address = exception->ExceptionRecord->ExceptionInformation[1];
  const auto handler = fault_handler.load(std::memory_order_acquire);
  if (!memory || !handler || !memory->IsMapped(address, 1)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  try {
    return handler(exception, reinterpret_cast<void *>(address))
               ? EXCEPTION_CONTINUE_EXECUTION
               : EXCEPTION_CONTINUE_SEARCH;
  } catch (...) {
    constexpr char message[] =
        "gpu_core: protected memory synchronization failed\n";
    DWORD written{};
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, sizeof(message) - 1,
              &written, nullptr);
    return EXCEPTION_CONTINUE_SEARCH;
  }
}
} // namespace

CacheMemoryBackend &CacheMemory() {
  auto *memory = backend.load(std::memory_order_acquire);
  if (!memory) {
    throw std::logic_error("GPU cache memory backend is not configured");
  }
  return *memory;
}

CacheMemoryScope::CacheMemoryScope(CacheMemoryBackend &supplied)
    : backend{&supplied} {
  CacheMemoryBackend *expected{};
  if (!GpuCore::backend.compare_exchange_strong(expected, backend)) {
    throw std::logic_error("A GPU cache memory backend is already active");
  }
}

CacheMemoryScope::~CacheMemoryScope() {
  if (fault_handler.load()) {
    std::terminate();
  }
  GpuCore::backend.store(nullptr, std::memory_order_release);
}

void *InstallCacheFaultHandler(CacheFaultHandler handler) {
  CacheFaultHandler expected{};
  if (!handler || !fault_handler.compare_exchange_strong(expected, handler)) {
    throw std::logic_error("A GPU cache fault handler is already active");
  }
  auto *registration = AddVectoredExceptionHandler(1, CacheException);
  if (!registration) {
    fault_handler.store(nullptr);
    throw std::runtime_error("Could not install GPU cache fault handler");
  }
  return registration;
}

void RemoveCacheFaultHandler(void *registration) {
  if (!RemoveVectoredExceptionHandler(registration)) {
    std::terminate();
  }
  fault_handler.store(nullptr, std::memory_order_release);
}

bool CacheFaultIsWrite(void *context) {
  return static_cast<EXCEPTION_POINTERS *>(context)
             ->ExceptionRecord->ExceptionInformation[0] == 1;
}
} // namespace Ketsui::GpuCore
