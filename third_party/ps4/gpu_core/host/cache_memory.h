#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace Ketsui::GpuCore {

class CacheMemoryBackend {
public:
  virtual ~CacheMemoryBackend() = default;
  virtual bool IsMapped(std::uint64_t address,
                        std::uint64_t bytes) const noexcept = 0;
  virtual std::uint64_t MappedBytes(std::uint64_t address,
                                   std::uint64_t maximum) const noexcept = 0;
  virtual void ReadSparse(std::uint64_t address, void *destination,
                          std::uint64_t bytes) = 0;
  // Descriptor traversal needs current GPU data, not a stale backing snapshot.
  virtual void ReadMetadata(std::uint64_t address, void *destination,
                            std::uint64_t bytes) = 0;
  virtual void ReadProgram(std::uint64_t address, void* destination,
                           std::uint64_t bytes) = 0;
  virtual bool WriteBacking(std::uint64_t address, const void *source,
                            std::uint64_t bytes) = 0;
  virtual void Protect(std::uint64_t address, std::uint64_t bytes,
                       unsigned permissions) = 0;
  virtual bool HandleAccess(std::uint64_t address, std::uint64_t bytes,
                            bool write, bool gpu_thread) = 0;
  virtual bool IsGpuThread() const noexcept = 0;
  virtual void RunGpuSync(std::function<void()> command) = 0;
};

CacheMemoryBackend &CacheMemory();

class CacheMemoryScope {
public:
  explicit CacheMemoryScope(CacheMemoryBackend &backend);
  ~CacheMemoryScope();
  CacheMemoryScope(const CacheMemoryScope &) = delete;
  CacheMemoryScope &operator=(const CacheMemoryScope &) = delete;

private:
  CacheMemoryBackend *backend;
};

using CacheFaultHandler = bool (*)(void *context, void *address);
void *InstallCacheFaultHandler(CacheFaultHandler handler);
void RemoveCacheFaultHandler(void *registration);
bool CacheFaultIsWrite(void *context);

} // namespace Ketsui::GpuCore
