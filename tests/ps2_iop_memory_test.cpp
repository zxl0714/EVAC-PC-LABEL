#include "emulator/core/iop_memory.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>

using ps2x::iop::detail::IopMemory;
namespace {
void Require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
}
int main() {
    try {
        IopMemory memory;
        const auto capacity=IopMemory::HeapLimit-IopMemory::HeapBase;
        const auto first=memory.allocate(262144,64);
        const auto second=memory.allocate(262144,64);
        Require(first && second && first!=second,"Initial IOP allocation failed");
        Require(memory.freeAllocation(first),"IOP free rejected its allocation");
        Require(memory.maxFreeMemory()==capacity-524288,
                "IOP largest free block ignored a released range");
        const auto recycled=memory.allocate(262144,64);
        Require(recycled==first,"IOP allocator did not reuse a released range");
        for(unsigned i=0;i<64;++i) {
            Require(memory.freeAllocation(recycled),"Repeated IOP free failed");
            Require(memory.allocate(262144,64)==recycled,"IOP temporary buffers exhausted the heap");
        }
        Require(memory.freeAllocation(second) && memory.freeAllocation(recycled),"IOP cleanup failed");
        Require(memory.maxFreeMemory()==capacity,"IOP free space did not recover");
        Require(!memory.freeAllocation(recycled),"Double IOP free was accepted");
        const auto fixed=IopMemory::HeapBase+0x40000u;
        Require(memory.allocate(64,64,fixed)==fixed,"Fixed IOP allocation failed");
        Require(!memory.allocate(64,64,fixed),"Overlapping fixed IOP allocation was accepted");
        Require(!memory.allocate(64,64,fixed+1),"Misaligned fixed IOP allocation was accepted");
        Require(memory.maxFreeMemory()==capacity-0x40040u,"Largest IOP gap was misreported");
        Require(!memory.allocate(UINT32_MAX,64),"Overflowing IOP size was accepted");
        Require(!memory.allocate(32,6),"Non-power-of-two IOP alignment was accepted");
        Require(!memory.allocate(64,64,UINT32_MAX-31),"Wrapping fixed IOP range was accepted");
        memory.reset();
        Require(memory.maxFreeMemory()==capacity,"IOP reset did not restore heap");
        Require(memory.allocate(capacity,16)==IopMemory::HeapBase,"Exact IOP capacity was rejected");
        Require(!memory.allocate(1,16),"IOP allocation crossed the heap limit");
        std::cout<<"IOP memory: released buffers, fixed ranges and capacity passed\n";
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
