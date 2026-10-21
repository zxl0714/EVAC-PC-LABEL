#include "tilemap.h"
#include <rex/ppc.h>
#include <rex/memory.h>
#include <stdexcept>

// Disc CA022100 only, immediately after 88051C30 allocates 32768 bytes and
// before the new pointer is stored. No existing map, native index or register
// is changed. This compatibility fix is independent of Frame Delay policy.
void SdojInitializeTilemap(PPCContext& ctx,std::uint8_t* base) {
    const auto address=ctx.r3.u32;
    if(!address)return; // Preserve the allocator's original failure result.
    constexpr auto bytes=sdoj::compat::TilemapBytes;
    auto* memory=rex::system::kernel_state()->memory();
    auto* heap=memory->LookupHeap(address);
    if(std::uint64_t(address)+bytes>0x100000000ull || !heap ||
       heap!=memory->LookupHeap(address+bytes-1) ||
       (std::uint32_t(heap->QueryRangeAccess(address,address+bytes-1))&3)!=3)
        throw std::runtime_error("SDOJ: newly allocated tilemap is not writable");
    sdoj::compat::InitializeTilemap({base+address,bytes});
}
