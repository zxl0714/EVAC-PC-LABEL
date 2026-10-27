#pragma once
#include <rex/memory.h>
#include <rex/system/xam/community_service.h>

namespace espgaluda2 {
// The verified download caller (820AB67C..820AB6A8) owns a 2048-byte
// path array at object+5876, preceded by view and followed by length.
// Unlike its upload path it makes only one BuildServerPath call and leaves
// length zero on first use. Restore the array capacity before each download;
// the shared storage API still validates/writes only the required bytes.
inline bool InitializeDownloadPathCapacity(rex::memory::Memory& memory,
    uint32_t info, uint32_t destination, uint32_t length) {
    constexpr uint32_t capacity = 2048;
    if (!info || uint64_t(info)+4 != destination ||
        uint64_t(destination)+capacity != length ||
        uint64_t(length)+4 > 0x100000000ull) return false;
    auto* heap = memory.LookupHeap(info);
    const auto end = length+3;
    if (!heap || heap != memory.LookupHeap(end) ||
        !rex::system::xam::CommunityPageAccessAllowed(
            uint32_t(heap->QueryRangeAccess(info,end)),true)) return false;
    rex::memory::store_and_swap<uint32_t>(memory.TranslateVirtual(length),capacity);
    return true;
}
}
