#include "backends/ps4/loader/image_memory.h"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
unsigned checks{};
void Check(bool value, const char* description) {
    ++checks;
    if (!value) throw std::runtime_error(description);
}
struct Reservations {
    std::vector<void*> addresses;
    void Keep(void* address) { if (address) addresses.push_back(address); }
    ~Reservations() { for (auto* address : addresses) VirtualFree(address, 0, MEM_RELEASE); }
};
void OccupiedCandidates() {
    Reservations occupied, images;
    for (std::uintptr_t address = 0x08000000; address <= 0x78000000; address += 0x08000000) {
        auto* block = VirtualAlloc(reinterpret_cast<void*>(address), 65536,
                                  MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        occupied.Keep(block);
        if (block) *static_cast<unsigned*>(block) = 0x12345678;
        // An already occupied candidate is suitable too; never release it.
        MEMORY_BASIC_INFORMATION info{};
        Check(VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) &&
              info.State != MEM_FREE, "Failed to occupy a legacy candidate");
    }
    for (unsigned i = 0; i < 3; ++i) {
        const auto result = cave::ps4::ReserveImageMemory(8 * 1024 * 1024 + 1);
        images.Keep(result.address);
        std::printf("occupied_candidates image=%u base=%p attempts=%u range_search=%u error=%lu\n",
                    i, result.address, result.attempt_count, unsigned(result.searched_range), result.error);
        Check(result.address != nullptr, "Fixed candidates occupied, but usable gaps were ignored");
        Check(result.searched_range && result.attempt_count == 15, "Fallback was not exercised");
        const auto base = reinterpret_cast<std::uintptr_t>(result.address);
        Check(base >= 65536 && base < cave::ps4::kImageAddressLimit &&
              result.bytes <= cave::ps4::kImageAddressLimit - base,
              "Image crosses signed 2 GiB limit");
        Check(base % 65536 == 0 && result.bytes == 8 * 1024 * 1024 + 65536,
              "Image reservation alignment");
        MEMORY_BASIC_INFORMATION info{};
        Check(VirtualQuery(result.address, &info, sizeof(info)) && info.State == MEM_RESERVE &&
              info.AllocationBase == result.address, "Reservation unexpectedly committed memory");
        auto* page = static_cast<unsigned char*>(VirtualAlloc(result.address, 4096, MEM_COMMIT, PAGE_READWRITE));
        Check(page == result.address, "Loader cannot commit image segments");
        page[0] = 0x53;
        page[4095] = 0xa7;
        Check(page[0] == 0x53 && page[4095] == 0xa7, "Committed segment is not writable");
    }
    for (auto* block : occupied.addresses)
        Check(*static_cast<unsigned*>(block) == 0x12345678, "Existing allocation overwritten");
}
void OrdinaryAndInvalidRequests() {
    for (const auto bytes : {0ull, cave::ps4::kMaximumImageBytes + 1, ~0ull}) {
        const auto result = cave::ps4::ReserveImageMemory(bytes);
        Check(!result.address && result.error == ERROR_INVALID_PARAMETER && !result.attempt_count,
              "Invalid image size was not rejected before allocation");
    }
    for (const auto bytes : {1ull, cave::ps4::kMaximumImageBytes}) {
        std::printf("ordinary_request bytes=%llu\n", bytes);
        Reservations images;
        const auto result = cave::ps4::ReserveImageMemory(bytes);
        images.Keep(result.address);
        Check(result.address && result.error == ERROR_SUCCESS, "Valid image size failed");
        const auto base = reinterpret_cast<std::uintptr_t>(result.address);
        Check(base >= 65536 && base < cave::ps4::kImageAddressLimit &&
              result.bytes <= cave::ps4::kImageAddressLimit - base, "Large image exceeds signed address range");
        MEMORY_BASIC_INFORMATION info{};
        Check(VirtualQuery(result.address, &info, sizeof(info)) && info.State == MEM_RESERVE,
              "Image allocation should not commit the entire image");
    }
}
void FillFreeLowRanges(Reservations& occupied) {
    // Reserve free low VA in this test process only: no large physical memory
    // commitment and no change to other processes' address spaces.
    for (std::uintptr_t cursor = 65536; cursor < cave::ps4::kImageAddressLimit;) {
        MEMORY_BASIC_INFORMATION info{};
        Check(VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) != 0,
              "Cannot inspect test address space");
        const auto end = (std::min)(reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize,
                                   cave::ps4::kImageAddressLimit);
        Check(end > cursor, "Non-advancing address-space query");
        const auto start = (cursor + 65535) & ~std::uintptr_t(65535);
        if (info.State == MEM_FREE && start < end) {
            const auto size = (end - start) & ~std::uintptr_t(65535);
            if (size) {
                auto* block = VirtualAlloc(reinterpret_cast<void*>(start), size, MEM_RESERVE, PAGE_NOACCESS);
                occupied.Keep(block);
                Check(block != nullptr, "Cannot reserve a free test range");
            }
        }
        cursor = end;
    }
}
void OffsetGameSizedGap() {
    Reservations gap_owner, occupied, images;
    constexpr std::size_t gap_bytes = 144 * 1024 * 1024;
    void* gap{};
    for (std::uintptr_t base = 0x11000000; !gap && base <= 0x61000000; base += 0x10000000)
        gap = VirtualAlloc(reinterpret_cast<void*>(base), gap_bytes, MEM_RESERVE, PAGE_NOACCESS);
    gap_owner.Keep(gap);
    Check(gap != nullptr, "Cannot establish a game-sized offset gap");
    FillFreeLowRanges(occupied);
    Check(VirtualFree(gap, 0, MEM_RELEASE) != 0, "Cannot open the offset gap");
    gap_owner.addresses.clear();
    // The supported 01.00 core's image span, including its zero-filled segments.
    const auto result = cave::ps4::ReserveImageMemory(146098004);
    images.Keep(result.address);
    Check(result.address == gap && result.searched_range && result.error == ERROR_SUCCESS,
          "Game-sized image must use a gap missed by all legacy base addresses");
    Check(result.regions_queried && result.largest_free_region == gap_bytes,
          "Free-region diagnostics lost the usable game-sized gap");
    Check(result.bytes <= gap_bytes, "Image crossed its free region");
    std::printf("game_sized_gap requested=146098004 reserved=%zu base=%p attempts=%u queried=%u\n",
                result.bytes, result.address, result.attempt_count, result.regions_queried);
}
void ExhaustedAddressRange() {
    Reservations occupied;
    FillFreeLowRanges(occupied);
    const auto result = cave::ps4::ReserveImageMemory(65536);
    occupied.Keep(result.address);
    Check(!result.address && result.searched_range && result.error != ERROR_SUCCESS,
          "Exhaustion must report failure rather than escape the address range");
    Check(result.regions_queried && result.regions_queried <= 32768 && !result.largest_free_region,
          "Exhaustion scan must be bounded and report no free region");
    std::printf("exhausted_range attempts=%u range_search=%u error=%lu\n",
                result.attempt_count, unsigned(result.searched_range), result.error);
}
void HoldImageAcrossStartupAllocations() {
    Reservations images;
    constexpr std::uint64_t core_bytes = 146098004;
    const auto main = cave::ps4::ReserveImageMemory(4 * 1024 * 1024);
    images.Keep(main.address);
    const auto core = cave::ps4::ReserveImageMemory(core_bytes);
    images.Keep(core.address);
    Check(main.address && core.address, "Cannot reserve startup image addresses");

    Reservations occupied;
    FillFreeLowRanges(occupied);
    const auto late = cave::ps4::ReserveImageMemory(core_bytes);
    images.Keep(late.address);
    Check(!late.address && late.searched_range && late.largest_free_region < core.bytes,
          "Late reservation should fail after low address space fills");
    MEMORY_BASIC_INFORMATION info{};
    Check(VirtualQuery(core.address, &info, sizeof(info)) && info.State == MEM_RESERVE &&
          info.AllocationBase == core.address && info.RegionSize >= core.bytes,
          "Startup reservation was lost or prematurely committed");
    auto* page = static_cast<unsigned char*>(VirtualAlloc(core.address + core.bytes - 4096, 4096,
                                                           MEM_COMMIT, PAGE_READWRITE));
    Check(page == core.address + core.bytes - 4096, "Loader cannot commit the reserved core");
    page[0] = 0x53;
    Check(page[0] == 0x53, "Committed core page is not writable");
}
}
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        std::puts("ordinary_requests_begin");
        OrdinaryAndInvalidRequests();
        std::puts("occupied_candidates_begin");
        OccupiedCandidates();
        std::puts("game_sized_gap_begin");
        OffsetGameSizedGap();
        std::puts("exhausted_range_begin");
        ExhaustedAddressRange();
        std::puts("startup_reservation_begin");
        HoldImageAcrossStartupAllocations();
        std::puts("reuse_begin");
        OrdinaryAndInvalidRequests(); // Releasing reservations permits reuse.
        std::printf("%u image reservation checks passed\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Image reservation test failed: %s\n", error.what());
        return 1;
    }
}
