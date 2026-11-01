#include "emulator/core/iop_memory.h"
#include "emulator/services/iop_module_loader.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>

using ps2x::iop::detail::IopMemory;
using ps2x::iop::detail::IopModuleLoader;
namespace {
void Require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
void Put16(std::vector<uint8_t>& bytes,size_t offset,uint16_t value) {
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
void Put32(std::vector<uint8_t>& bytes,size_t offset,uint32_t value) {
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
std::vector<uint8_t> ModuleFixture() {
    std::vector<uint8_t> bytes(0x110);
    const uint8_t ident[]{0x7f,'E','L','F',1,1,1};
    std::memcpy(bytes.data(),ident,sizeof(ident));
    Put16(bytes,16,0xff80);Put16(bytes,18,8);Put32(bytes,20,1);
    Put32(bytes,28,52);Put16(bytes,40,52);Put16(bytes,42,32);Put16(bytes,44,1);
    Put16(bytes,46,40);
    Put32(bytes,52,1);Put32(bytes,56,0x100);Put32(bytes,68,16);
    Put32(bytes,72,0x1000);Put32(bytes,76,7);Put32(bytes,80,0x100);
    Put32(bytes,0x100,0x12345678);
    return bytes;
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
        memory.reset();
        auto image=ModuleFixture();
        const auto firstModule=IopModuleLoader::load(image,memory,IopMemory::HeapBase);
        Require(bool(firstModule),"Authored relocatable IOP module failed to load");
        const auto upload=memory.allocate(262144,64);
        Require(upload>=firstModule.base+firstModule.size,"IOP buffer overwrote module BSS or image gaps");
        memory.write32(upload,0xbadcafe);
        const auto lateModule=IopModuleLoader::load(image,memory,firstModule.nextModuleCursor);
        Require(bool(lateModule) && lateModule.base>=upload+262144,"Late IOP module collided with a live buffer");
        Require(memory.read32(upload)==0xbadcafe && memory.read32(firstModule.base)==0x12345678,
                "Late module loading changed an existing owner");
        auto fixedImage=image;Put16(fixedImage,16,2);Put32(fixedImage,60,firstModule.base);
        Require(!IopModuleLoader::load(fixedImage,memory,lateModule.nextModuleCursor),
                "Fixed IOP image overwrote another module");
        Require(memory.read32(firstModule.base)==0x12345678,"Rejected fixed image corrupted a module");
        Require(memory.freeAllocation(upload),"Upload buffer could not be released");
        const auto recycledModule=IopModuleLoader::load(image,memory,lateModule.nextModuleCursor);
        Require(bool(recycledModule) && recycledModule.base==upload,"IOP module did not reuse a freed buffer range");
        const auto beforeFailure=memory.maxFreeMemory();
        auto malformed=image;Put32(malformed,56,0x1000);
        Require(!IopModuleLoader::load(malformed,memory,recycledModule.nextModuleCursor),"Malformed module was accepted");
        Require(memory.maxFreeMemory()==beforeFailure,"Failed IOP loading leaked its reserved image");
        memory.reset();
        image=ModuleFixture();Put32(image,72,0x50000);
        Require(bool(IopModuleLoader::load(image,memory,IopMemory::HeapBase)),
                "Representative IOP code image did not fit");
        constexpr uint32_t liveBuffers[]{0x2040,0x1040,0x840,0x840,0x840,0x840,0x840,0x840,
            0x40000,0x840,0x8d0,0x840,0x30c40,0x8d0,0x3040,0x1000,0x2000,0x40000,0x800,0x800};
        for(const auto bytes:liveBuffers)
            Require(memory.allocate(bytes,64)!=0,"IOP service buffers did not fit alongside code");
        Require(memory.allocate(262144,64)!=0,"IOP sound upload buffer was starved by a fixed partition");
        std::cout<<"IOP memory: released buffers, fixed ranges and capacity passed\n";
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
