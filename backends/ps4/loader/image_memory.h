#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cave::ps4 {
inline constexpr std::uint64_t kMaximumImageBytes = 512ull * 1024 * 1024;
inline constexpr std::uintptr_t kImageAddressLimit = 0x80000000;
inline constexpr std::size_t kImageAllocationAlignment = 65536;

struct ImageReservation {
    struct Attempt { std::uintptr_t address{}; unsigned long error{}; };
    unsigned char* address{};
    std::size_t bytes{};
    std::array<Attempt, 15> preferred_attempts{};
    unsigned attempt_count{};
    bool searched_range{};
    unsigned regions_queried{};
    std::size_t largest_free_region{};
    unsigned long error{};
};

// Reserves address space only. The loader commits/protects each segment and
// owns the reservation until VirtualFree(..., MEM_RELEASE).
ImageReservation ReserveImageMemory(std::uint64_t bytes) noexcept;
}
