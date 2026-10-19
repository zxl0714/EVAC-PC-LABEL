#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace KetsuiGcnHost {
using Reader = bool(*)(void*, std::uint64_t, void*, std::size_t);
struct Memory { Reader read{}; void* context{}; };
inline thread_local Memory memory;
struct MemoryScope {
    Memory previous;
    explicit MemoryScope(Memory next) : previous(memory) { memory = next; }
    ~MemoryScope() { memory = previous; }
    MemoryScope(const MemoryScope&) = delete;
    MemoryScope& operator=(const MemoryScope&) = delete;
};
inline void Read(std::uint64_t address, void* output, std::size_t bytes) {
    if (!memory.read || !memory.read(memory.context, address, output, bytes)) { throw std::runtime_error("GCN constant memory is not readable through the host interface"); }
}
}
