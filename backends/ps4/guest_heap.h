#pragma once

#include <windows.h>

#include <cstddef>
#include <mutex>
#include <unordered_map>

// Storage owned by the game never goes through the host C++ allocator. In
// particular, an invalid guest delete must not be forwarded to the process heap.
class GuestHeap {
public:
    GuestHeap();
    ~GuestHeap();
    GuestHeap(const GuestHeap&) = delete;
    GuestHeap& operator=(const GuestHeap&) = delete;

    void* Allocate(std::size_t size) noexcept;
    void* AllocateAligned(std::size_t size, std::size_t alignment) noexcept;
    bool Release(void* pointer) noexcept;

private:
    HANDLE heap_{};
    std::mutex mutex_;
    struct Allocation { void* base; std::size_t size, alignment; };
    std::unordered_map<void*, Allocation> allocations_;
};

bool GuestHeapSelfTest();
