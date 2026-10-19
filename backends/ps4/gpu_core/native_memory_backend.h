#pragma once
#include "../native_memory.h"
#include "cache_memory.h"
#include <atomic>

namespace Ketsui::GpuCore {
// Copies through the backing interface, not a counter of direct CPU loads.
struct NativeBackingTraffic {
  std::uint64_t read_calls{}, read_bytes{}, write_calls{}, write_bytes{};
};
// GPU-owned state is touched only through sync, including lifecycle callbacks.
// access receives every CPU virtual alias intersecting the physical range.
// Consumers still must solve GPU-to-GPU alias bindings before enabling draws
// through multiple GPU virtual addresses for the same backing.
struct NativeCacheHooks {
  std::function<void(std::uint64_t, void *, std::uint64_t)> read_program;
  std::function<void(std::function<void()>)> sync;
  std::function<bool()> is_gpu_thread;
  std::function<void(const NativeMemoryRangeInfo &)> mapped;
  std::function<void(std::span<const NativeMemoryRangeInfo>)> before_change;
  std::function<void(std::span<const NativeMemoryRangeInfo>, bool)> access;
  std::function<void()> drain;
};
class NativeMemoryCacheBackend final : public CacheMemoryBackend {
  struct Observer;
  std::shared_ptr<Observer> observer;
  bool attached{};
  std::atomic<std::uint32_t> read_faults{}, write_faults{};
  std::atomic<std::uint64_t> metadata_reads{};
  std::atomic<std::uint64_t> backing_reads{}, backing_read_bytes{},
      backing_writes{}, backing_write_bytes{};

public:
  explicit NativeMemoryCacheBackend(NativeCacheHooks hooks);
  ~NativeMemoryCacheBackend();
  NativeMemoryCacheBackend(const NativeMemoryCacheBackend &) = delete;
  NativeMemoryCacheBackend &
  operator=(const NativeMemoryCacheBackend &) = delete;
  void Attach();
  void Detach();
  std::pair<std::uint32_t, std::uint32_t> FaultCounts() const noexcept {
    return {read_faults.load(), write_faults.load()};
  }
  std::uint64_t MetadataReads() const noexcept { return metadata_reads.load(); }
  NativeBackingTraffic Traffic() const noexcept {
    return {backing_reads.load(), backing_read_bytes.load(),
            backing_writes.load(), backing_write_bytes.load()};
  }
  bool IsMapped(std::uint64_t, std::uint64_t) const noexcept override;
  std::uint64_t MappedBytes(std::uint64_t,
                            std::uint64_t) const noexcept override;
  void ReadSparse(std::uint64_t, void *, std::uint64_t) override;
  void ReadMetadata(std::uint64_t, void *, std::uint64_t) override;
  void ReadProgram(std::uint64_t, void *, std::uint64_t) override;
  bool WriteBacking(std::uint64_t, const void *, std::uint64_t) override;
  void Protect(std::uint64_t, std::uint64_t, unsigned) override;
  bool HandleAccess(std::uint64_t, std::uint64_t, bool, bool) override;
  bool IsGpuThread() const noexcept override;
  void RunGpuSync(std::function<void()>) override;
};
} // namespace Ketsui::GpuCore
