#include "image_memory.h"
#include <Windows.h>

namespace cave::ps4 {
ImageReservation ReserveImageMemory(std::uint64_t bytes) noexcept {
    ImageReservation result;
    if (!bytes || bytes > kMaximumImageBytes) {
        result.error = ERROR_INVALID_PARAMETER;
        return result;
    }
    result.bytes = static_cast<std::size_t>((bytes + kImageAllocationAlignment - 1) &
                                          ~(std::uint64_t(kImageAllocationAlignment) - 1));
    // Preserve existing image placement before looking for other free ranges.
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (std::uintptr_t preferred = pass ? 0x08000000 : 0x10000000;
             preferred <= 0x78000000; preferred += 0x10000000) {
            if (result.bytes > kImageAddressLimit - preferred) continue;
            auto& attempt = result.preferred_attempts[result.attempt_count++];
            attempt.address = preferred;
            result.address = static_cast<unsigned char*>(VirtualAlloc(
                reinterpret_cast<void*>(preferred), result.bytes, MEM_RESERVE, PAGE_NOACCESS));
            if (result.address) { result.error = ERROR_SUCCESS; return result; }
            attempt.error = result.error = GetLastError();
        }
    }
    // Fixed candidates can all be occupied even when large usable gaps remain.
    // Query actual regions instead; VirtualAlloc remains the atomic ownership
    // check. A concurrent allocation may invalidate a query, so advance by at
    // least one allocation unit and re-query without ever looping in place.
    result.searched_range = true;
    result.error = ERROR_NOT_ENOUGH_MEMORY;
    for (std::uintptr_t cursor = kImageAllocationAlignment;
         result.bytes <= kImageAddressLimit - cursor;) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info))) {
            result.error = GetLastError();
            break;
        }
        ++result.regions_queried;
        const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const auto end = base + (info.RegionSize < kImageAddressLimit - base ?
                                 info.RegionSize : kImageAddressLimit - base);
        if (end <= cursor) { result.error = ERROR_INVALID_ADDRESS; break; }
        if (info.State == MEM_FREE && end - cursor > result.largest_free_region)
            result.largest_free_region = end - cursor;
        if (info.State == MEM_FREE && result.bytes <= end - cursor) {
            result.address = static_cast<unsigned char*>(VirtualAlloc(
                reinterpret_cast<void*>(cursor), result.bytes, MEM_RESERVE, PAGE_NOACCESS));
            if (result.address) { result.error = ERROR_SUCCESS; return result; }
            result.error = GetLastError();
            if (result.error != ERROR_INVALID_ADDRESS) break;
            cursor += kImageAllocationAlignment;
        } else {
            cursor = (end + kImageAllocationAlignment - 1) & ~(kImageAllocationAlignment - 1);
        }
    }
    return result;
}
}
