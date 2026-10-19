#include "guest_heap.h"

#include <limits>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

GuestHeap::GuestHeap() : heap_(HeapCreate(0, 0, 0)) {
    if (!heap_) {
        throw std::runtime_error("Cannot create the private game heap");
    }
    static_assert(MEMORY_ALLOCATION_ALIGNMENT >= 16);
}

GuestHeap::~GuestHeap() {
    HeapDestroy(heap_);
}

void* GuestHeap::Allocate(std::size_t size) noexcept {
    return AllocateAligned(size, 16);
}
void* GuestHeap::AllocateAligned(std::size_t size, std::size_t alignment) noexcept {
    // No Windows user allocation can span more than the signed address range.
    // Reject this before asking the OS, without an overflowing size calculation.
    const auto maximum = static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)());
    if (!alignment || (alignment & (alignment - 1)) || alignment > maximum) { return nullptr; }
    alignment = (std::max)(alignment, std::size_t{16});
    const auto payload = size ? size : 1;
    if (payload > maximum - (alignment - 1)) {
        return nullptr;
    }
    try {
        std::lock_guard lock(mutex_);
        void* base = HeapAlloc(heap_, 0, payload + alignment - 1);
        if (!base) {
            return nullptr;
        }
        const auto address = reinterpret_cast<std::uintptr_t>(base);
        auto* pointer = reinterpret_cast<void*>((address + alignment - 1) & ~(alignment - 1));
        try {
            if (!allocations_.emplace(pointer, Allocation{base, size, alignment}).second) {
                HeapFree(heap_, 0, base); return nullptr;
            }
        } catch (...) {
            HeapFree(heap_, 0, base);
            return nullptr;
        }
        return pointer;
    } catch (...) {
        return nullptr;
    }
}

bool GuestHeap::Release(void* pointer) noexcept {
    if (!pointer) {
        return true;
    }
    try {
        std::lock_guard lock(mutex_);
        const auto allocation = allocations_.find(pointer);
        if (allocation == allocations_.end() || !HeapFree(heap_, 0, allocation->second.base)) {
            return false;
        }
        allocations_.erase(allocation);
        return true;
    } catch (...) {
        return false;
    }
}

bool GuestHeapSelfTest() {
    GuestHeap heap;
    bool okay = true; int passed{};
    const auto check = [&](bool value, const char* name) {
        std::printf("{\"event\":\"heap_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
        okay &= value; if (value) { ++passed; }
    };
    auto* first = heap.AllocateAligned(35, 128);
    auto* second = heap.AllocateAligned(35, 65536);
    check(first && second && reinterpret_cast<std::uintptr_t>(first) % 128 == 0 &&
          reinterpret_cast<std::uintptr_t>(second) % 65536 == 0, "aligned_user_pointers");
    if (first && second) {
        std::memset(first, 0x5a, 35); std::memset(second, 0xa5, 35);
        check(static_cast<unsigned char*>(first)[34] == 0x5a && static_cast<unsigned char*>(second)[34] == 0xa5,
              "requested_ranges_are_independent_and_writable");
    } else { check(false, "requested_ranges_are_independent_and_writable"); }
    check(first && !heap.Release(static_cast<char*>(first) + 1) && heap.Release(first) && !heap.Release(first),
          "interior_and_double_free_rejected");
    check(heap.Release(second) && heap.Release(nullptr), "aligned_and_null_free");
    check(!heap.AllocateAligned((std::numeric_limits<std::size_t>::max)(), 128) && !heap.AllocateAligned(1, 0) &&
          !heap.AllocateAligned(1, 70), "invalid_alignment_and_overflow_preserve_heap");
    auto* zero_a = heap.AllocateAligned(0, 128); auto* zero_b = heap.AllocateAligned(0, 128);
    check(zero_a && zero_b && zero_a != zero_b && heap.Release(zero_a) && heap.Release(zero_b), "zero_size_distinct_allocations");
    std::atomic<bool> threads_ok{true};
    std::array<std::thread, 4> threads;
    for (auto& thread : threads) {
        thread = std::thread([&] {
            for (unsigned i = 0; i < 200; ++i) {
                const std::size_t alignment = 1ull << (4 + i % 9);
                auto* pointer = heap.AllocateAligned(200, alignment);
                if (!pointer) { threads_ok.store(false); continue; }
                std::memset(pointer, 0x47, 200);
                if (reinterpret_cast<std::uintptr_t>(pointer) % alignment || !heap.Release(pointer)) { threads_ok.store(false); }
            }
        });
    }
    for (auto& thread : threads) { thread.join(); }
    check(threads_ok.load(), "concurrent_allocations_and_release");
    std::printf("{\"event\":\"heap_self_test\",\"passed\":%d,\"success\":%s}\n", passed, okay ? "true" : "false");
    return okay;
}
