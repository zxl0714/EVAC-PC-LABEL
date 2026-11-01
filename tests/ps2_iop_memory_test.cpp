#include "emulator/core/iop_memory.h"
#include "emulator/services/iop_module_loader.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <array>
#include <algorithm>

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
void AudioBridge() {
    IopMemory memory;
    const auto spuAddress=[](unsigned core,uint32_t reg){return 0x1f900000u+core*0x400u+reg;};
    const auto setAddress=[&](unsigned core,uint32_t reg,uint32_t value){
        memory.write16(spuAddress(core,reg),uint16_t(value>>17));
        memory.write16(spuAddress(core,reg+2),uint16_t(value>>1));
    };
    const auto chcr=[](unsigned core){return core?0x1f801508u:0x1f8010c8u;};
    std::array<uint8_t,16> sample{};
    for(size_t i=0;i<sample.size();++i)sample[i]=uint8_t(i*13+5);
    for(size_t i=0;i<sample.size();++i)memory.write8((IopMemory::RamSize-8u+uint32_t(i))&(IopMemory::RamSize-1u),sample[i]);
    for(unsigned core=0;core<2;++core) {
        memory.write16(spuAddress(core,0x19a),0x8020);
        Require(memory.read16(spuAddress(core,0x19a))==0x8020,"SPU2 halfword control readback failed");
        setAddress(core,0x1a8,0x5000+core*0x100);
        memory.write32(chcr(core)-8,IopMemory::RamSize-8);
        memory.write32(chcr(core)-4,0x10004);
        memory.write32(chcr(core),0x1000201);
        Require(!(memory.read32(chcr(core))&0x1000000),"Completed sample DMA remained busy");
        Require(memory.read32(chcr(core)-8)==8,"Sample DMA failed IOP RAM wrap");
    }
    const auto event0=memory.takeDmaStart(),event1=memory.takeDmaStart();
    Require(event0 && event1 && event0->irq==0x24 && event1->irq==0x28 && !memory.takeDmaStart(),
            "Simultaneous sound DMA completions were lost");
    for(unsigned core=0;core<2;++core) {
        memory.write16(spuAddress(core,0x19a),0x8030);
        setAddress(core,0x1a8,0x5000+core*0x100);
        memory.write32(chcr(core)-8,0x9000+core*32);
        memory.write32(chcr(core),0x1000200);
        std::array<uint8_t,16> received{};
        Require(memory.readRam(0x9000+core*32,received.data(),received.size()) && received==sample,
                "IOP->SPU->IOP DMA round-trip changed sample data");
    }
    // DMA started before ATTR is configured must stay busy, then resume.
    memory.reset();
    memory.write32(0x10000,0x34343434);
    memory.write32(chcr(0)-8,0x10000);memory.write32(chcr(0)-4,0x10001);
    memory.write32(chcr(0),0x1000201);
    Require((memory.read32(chcr(0))&0x1000000) && !memory.takeDmaStart(),"Stopped SPU2 consumed DMA data");
    memory.write16(spuAddress(0,0x19a),0x8020);
    Require(!(memory.read32(chcr(0))&0x1000000) && memory.takeDmaStart(),"ATTR failed to resume pending sound DMA");

    for(unsigned core=0;core<2;++core) {
        memory.reset();
        std::vector<uint8_t> stream(2048,0x24);
        Require(memory.writeRam(0x10000,stream.data(),stream.size()),"Authored stream upload failed");
        memory.write16(spuAddress(core,0x19a),0x8000);
        memory.write16(spuAddress(core,0x1b0),uint16_t(core+1));
        memory.write32(chcr(core)-8,0x10000);memory.write32(chcr(core)-4,0x10200);
        memory.write32(chcr(core),0x1000201);
        Require((memory.read32(chcr(core))&0x1000000) && !memory.takeDmaStart(),"AutoDMA completed before its input was consumed");
        memory.advanceAudioTo(256u*768u);
        Require(!(memory.read32(chcr(core))&0x1000000) && memory.takeDmaStart(),"AutoDMA did not resume on the sound clock");
        std::array<int16_t,512> pcm{};
        Require(memory.readPcm(pcm.data(),256)==256 && std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;}),
                "Real sound DMA did not reach queued PCM");
    }
    memory.reset();
    std::array<int16_t,128> pcm{};
    memory.advanceAudioTo(64u*768u-1u);
    Require(memory.readPcm(pcm.data(),64)==0,"Sound clock advanced early");
    memory.advanceAudioTo(64u*768u);
    Require(memory.readPcm(pcm.data(),64)==64,"Sound clock lost its exact sample boundary");
    memory.advanceAudioTo(128u*768u);memory.reset();
    Require(memory.readPcm(pcm.data(),64)==0,"IOP reset replayed stale PCM");
    std::cout<<"IOP sound: real DMA, wrap, dual completion, AutoDMA backpressure, PCM clock/reset passed\n";
}
}
#include "ps2_iop_audio_fixture.h"
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--bench-idle") { IopIdleContract(true); return 0; }
        AudioBridge();
        AudioInsideGuestCall();
        IopIdleContract();
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
