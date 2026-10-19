#pragma once
#include "cache_memory.h"
#include "common/enum.h"
#include "common/types.h"

namespace Core {
enum class MemoryPermission : u32 {
  None = 0,
  Read = 1,
  Write = 2,
  ReadWrite = 3
};
DECLARE_ENUM_FLAG_OPERATORS(MemoryPermission)

class MemoryManager {
public:
  u64 ClampRangeSize(VAddr address, u64 size) {
    return Ketsui::GpuCore::CacheMemory().MappedBytes(address, size);
  }
  bool IsValidGpuMapping(VAddr address, u64 size) {
    return Ketsui::GpuCore::CacheMemory().IsMapped(address, size ? size : 1);
  }
  void CopySparseMemory(VAddr address, u8 *destination, u64 bytes) {
    Ketsui::GpuCore::CacheMemory().ReadSparse(address, destination, bytes);
  }
  bool TryWriteBacking(void *address, const void *source, u64 bytes) {
    return Ketsui::GpuCore::CacheMemory().WriteBacking(
        reinterpret_cast<VAddr>(address), source, bytes);
  }
};
class Memory {
public:
  static MemoryManager *Instance() {
    static MemoryManager manager;
    return &manager;
  }
};
} // namespace Core
