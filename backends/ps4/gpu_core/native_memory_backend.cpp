#include "native_memory_backend.h"
#include <Windows.h>
#include <algorithm>
#include <stdexcept>

namespace Ketsui::GpuCore {
struct NativeMemoryCacheBackend::Observer final : NativeGpuMemoryObserver {
  NativeCacheHooks hooks;
  explicit Observer(NativeCacheHooks supplied) : hooks{std::move(supplied)} {
    if (!hooks.sync || !hooks.is_gpu_thread || !hooks.mapped ||
        !hooks.before_change || !hooks.access || !hooks.drain) {
      throw std::invalid_argument(
          "Native GPU memory requires complete cache callbacks");
    }
  }
  void Mapped(const NativeMemoryRangeInfo &range) override {
    hooks.sync([&] { hooks.mapped(range); });
  }
  void BeforeChange(std::span<const NativeMemoryRangeInfo> ranges) override {
    hooks.sync([&] { hooks.before_change(ranges); });
  }
  void Access(std::span<const NativeMemoryRangeInfo> ranges, bool write) {
    if (ranges.size() == 1) {
      const auto aliases = NativeMemoryAliases(ranges[0].address, ranges[0].bytes);
      hooks.sync([&] { hooks.access(aliases, write); });
      return;
    }
    std::vector<NativeMemoryRangeInfo> aliases;
    for (const auto &range : ranges) {
      auto found = NativeMemoryAliases(range.address, range.bytes);
      aliases.insert(aliases.end(), found.begin(), found.end());
    }
    hooks.sync([&] { hooks.access(aliases, write); });
  }
  void BeforeHostWrite(std::span<const NativeMemoryRangeInfo> ranges) override {
    Access(ranges, true);
  }
  void AfterHostWrite(std::span<const NativeMemoryRangeInfo> ranges) override {
    Access(ranges, true);
  }
  void Drain() override {
    hooks.sync([&] { hooks.drain(); });
  }
};
NativeMemoryCacheBackend::NativeMemoryCacheBackend(NativeCacheHooks hooks)
    : observer{std::make_shared<Observer>(std::move(hooks))} {}
NativeMemoryCacheBackend::~NativeMemoryCacheBackend() {
  // A live attachment must be drained while the GPU callbacks are alive.
  if (attached) {
    std::terminate();
  }
}
void NativeMemoryCacheBackend::Attach() {
  if (attached) {
    throw std::logic_error("Native GPU memory already attached");
  }
  bool connected{};
  try {
    connected = AttachNativeGpuMemory(observer);
  } catch (...) {
    // Attach publishes the observer before callbacks can arm guards. Retire a
    // partially initialized callback sequence while its GPU state still lives.
    try {
      DetachNativeGpuMemory(observer.get());
    } catch (...) {
      std::terminate();
    }
    throw;
  }
  if (!connected) {
    throw std::runtime_error("Native GPU memory attachment requires unpinned "
                             "memory and no other backend");
  }
  attached = true;
}
void NativeMemoryCacheBackend::Detach() {
  if (!attached) {
    return;
  }
  DetachNativeGpuMemory(observer.get());
  attached = false;
}
bool NativeMemoryCacheBackend::IsMapped(std::uint64_t address,
                                        std::uint64_t bytes) const noexcept {
  NativeMemoryRangeInfo info;
  try {
    if (!bytes || bytes > UINT64_MAX - address) {
      return false;
    }
    const auto end = address + bytes;
    for (auto cursor = address; cursor < end;) {
      if (!QueryNativeMemoryRange(cursor, 1, &info)) {
        return false;
      }
      cursor += std::min(end - cursor,
                         info.mapping_bytes - (cursor - info.mapping_address));
    }
    return true;
  } catch (...) {
    return false;
  }
}
void NativeMemoryCacheBackend::ReadSparse(std::uint64_t address, void *data,
                                          std::uint64_t bytes) {
  if (!ReadNativeGpuMemory(address, data, bytes)) {
    throw std::runtime_error(
        "GPU read of native memory without GPU read permission");
  }
  ++backing_reads;
  backing_read_bytes += bytes;
}
std::uint64_t
NativeMemoryCacheBackend::MappedBytes(std::uint64_t address,
                                      std::uint64_t maximum) const noexcept {
  if (maximum > UINT64_MAX - address) {
    maximum = UINT64_MAX - address;
  }
  try {
    const auto end = address + maximum;
    auto cursor = address;
    while (cursor < end) {
      NativeMemoryRangeInfo info;
      if (!QueryNativeMemoryRange(cursor, 1, &info)) {
        break;
      }
      cursor += std::min(end - cursor,
                         info.mapping_bytes - (cursor - info.mapping_address));
    }
    return cursor - address;
  } catch (...) {
    return 0;
  }
}
bool NativeMemoryCacheBackend::WriteBacking(std::uint64_t address,
                                            const void *data,
                                            std::uint64_t bytes) {
  const bool written = WriteNativeGpuMemory(address, data, bytes);
  if (written) {
    ++backing_writes;
    backing_write_bytes += bytes;
  }
  return written;
}
void NativeMemoryCacheBackend::ReadMetadata(std::uint64_t address, void *data,
                                            std::uint64_t bytes) {
  RunGpuSync([&] {
    NativeMemoryRangeInfo single;
    if (QueryNativeMemoryRange(address, bytes, &single)) {
      observer->Access(std::span{&single, 1}, false);
      ReadSparse(address, data, bytes);
      ++metadata_reads;
      return;
    }
    if (!IsMapped(address, bytes)) {
      throw std::runtime_error(
          "Shader descriptor read outside registered GPU memory");
    }
    const auto end = address + bytes;
    for (auto cursor = address; cursor < end;) {
      NativeMemoryRangeInfo info;
      if (!QueryNativeMemoryRange(cursor, 1, &info)) {
        throw std::runtime_error("Shader descriptor mapping disappeared");
      }
      info.bytes = std::min(end - cursor, info.mapping_bytes -
                                              (cursor - info.mapping_address));
      observer->Access(std::span{&info, 1}, false);
      cursor += info.bytes;
    }
    ReadSparse(address, data, bytes);
    ++metadata_reads;
  });
}
void NativeMemoryCacheBackend::Protect(std::uint64_t address,
                                       std::uint64_t bytes,
                                       unsigned permissions) {
  if (!ProtectNativeGpuMemory(address, bytes, permissions)) {
    throw std::runtime_error("Invalid native GPU cache protection range");
  }
}
void NativeMemoryCacheBackend::ReadProgram(std::uint64_t address, void *output,
                                           std::uint64_t bytes) {
  if (IsMapped(address, bytes)) {
    ReadMetadata(address, output, bytes);
    return;
  }
  if (!observer->hooks.read_program) {
    throw std::runtime_error("No bounded program reader for this host image");
  }
  observer->hooks.read_program(address, output, bytes);
}
bool NativeMemoryCacheBackend::HandleAccess(std::uint64_t address,
                                            std::uint64_t bytes, bool write,
                                            bool) {
  NativeMemoryRangeInfo info;
  if (!QueryNativeMemoryRange(address, bytes, &info)) {
    return false;
  }
  if (!(info.logical_protection & (write ? 2u : 3u))) {
    return false;
  }
  if (!NativeGpuMemoryFault(address, write)) {
    // Another CPU's already queued fault may have removed the guard before
    // this handler acquired the metadata lock. Resume only if Windows now
    // actually allows this access; never consume a genuine RO/NA violation.
    MEMORY_BASIC_INFORMATION current{};
    if (!VirtualQuery(reinterpret_cast<void *>(address), &current,
                      sizeof(current)) ||
        current.State != MEM_COMMIT || (current.Protect & PAGE_GUARD)) {
      return false;
    }
    const auto rights = current.Protect & 0xff;
    return rights == PAGE_READWRITE || (!write && rights == PAGE_READONLY);
  }
  if (write) {
    ++write_faults;
  } else {
    ++read_faults;
  }
  observer->Access(std::span{&info, 1}, write);
  return true;
}
bool NativeMemoryCacheBackend::IsGpuThread() const noexcept {
  return observer->hooks.is_gpu_thread();
}
void NativeMemoryCacheBackend::RunGpuSync(std::function<void()> command) {
  observer->hooks.sync(std::move(command));
}
} // namespace Ketsui::GpuCore
