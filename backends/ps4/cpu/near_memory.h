#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace cave::ps4::cpu {
// Every guest detour uses rel32. Reserve whole 64 KiB allocation units within
// reach, also allowing for the offsets of individual adapters inside the unit.
inline std::uint8_t *AllocateNear(std::uintptr_t address, std::size_t bytes, DWORD allocation,
                                  DWORD protection) {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto unit = static_cast<std::uintptr_t>(system.dwAllocationGranularity);
    const auto low = (std::max)(reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress),
                                address > 0x70000000 ? address - 0x70000000 : 0);
    const auto high =
        (std::min)(reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress), address + 0x70000000);
    for (auto begin : {address, low}) {
        auto cursor = (begin + unit - 1) & ~(unit - 1);
        while (cursor < high && bytes <= high - cursor) {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<void *>(cursor), &region, sizeof(region)))
                break;
            if (region.State == MEM_FREE && bytes <= region.RegionSize) {
                auto *p = VirtualAlloc(reinterpret_cast<void *>(cursor), bytes, allocation, protection);
                if (p)
                    return static_cast<std::uint8_t *>(p);
            }
            const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
            if (end <= cursor)
                break;
            cursor = (end + unit - 1) & ~(unit - 1);
        }
    }
    throw std::runtime_error("Cannot reserve a nearby CPU adapter arena");
}
} // namespace cave::ps4::cpu
