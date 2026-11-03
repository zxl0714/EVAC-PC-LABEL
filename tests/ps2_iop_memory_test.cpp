#include "emulator/core/iop_memory.h"
#include "emulator/services/iop_module_loader.h"
#include "emulator/imports/iop_sysclib.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_kernel.h"
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

void SysclibCharacterFlags() {
    IopMemory memory;
    ps2x::iop::detail::IopSysclib library(memory);
    ps2x::iop::detail::IopCpuState cpu{};
    for (const auto [character,expected] : std::array<std::pair<uint32_t,uint32_t>,13>{{
        {'0',4},{'1',4},{'9',4},{'A',0x41},{'f',0x42},{'G',1},{'g',2},
        {' ',0x18},{'\t',8},{'!',0x10},{0,0x20},{127,0x20},{0xffffffff,0}}}) {
        cpu.gpr[4]=character;
        Require(library.dispatchImport(8,cpu) && cpu.gpr[2]==expected,
                "IOP sysclib character flags changed module numeric-option parsing");
    }
    std::cout<<"IOP sysclib digit/letter/space/control classification: PASS\n";
}
void NopRetirement() {
    using ps2x::iop::detail::IopCpuState;
    IopMemory memory;ps2x::iop::detail::IopCpuCore core(memory);
    for(const bool delay:{false,true})for(const bool pending:{false,true})
    for(const uint32_t reg:{0u,1u,31u})for(const bool stopped:{false,true}) {
        IopCpuState cpu{};
        cpu.gpr.fill(0x44332211);cpu.pc=0xfffffffcu;
        cpu.branchPending=delay;cpu.branchTarget=0x12345678;
        cpu.pendingLoad=pending;cpu.pendingLoadReg=reg;cpu.pendingLoadValue=0xabcdef01;
        cpu.exception=true;cpu.yielded=true;cpu.stopped=stopped;
        Require(core.executeInstruction(cpu,0)==!stopped,"NOP changed stopped state");
        Require(cpu.pc==(delay?0x12345678u:0u) && !cpu.branchPending,
                "NOP lost delayed branch or PC wrap");
        Require(cpu.branchTarget==(delay?0x12345678u:0u),"NOP changed branch-target retirement");
        Require(!cpu.exception && !cpu.yielded && !cpu.pendingLoad && cpu.gpr[0]==0,
                "NOP retained transient CPU state");
        for(uint32_t i=1;i<32;++i)
            Require(cpu.gpr[i]==(pending && reg==i?0xabcdef01u:0x44332211u),
                    "NOP lost a pending load or modified another register");
        Require(cpu.pendingLoadReg==reg && cpu.pendingLoadValue==0xabcdef01,
                "NOP modified inactive load metadata");
    }
    std::cout<<"IOP NOP: delayed branch, load retirement, zero register, PC wrap and stopped state: PASS\n";
}
void ReadyElectionInvalidation() {
    IopMemory memory;
    ps2x::iop::detail::IopKernel kernel(memory);
    ps2x::iop::detail::IopCpuState call{};
    constexpr uint32_t descriptor=0x18000, sentinel=0x1fffff00;
    auto create=[&](uint32_t priority,uint32_t entry) {
        for(unsigned i=0;i<5;++i)memory.write32(descriptor+4*i,0);
        memory.write32(descriptor+8,entry);memory.write32(descriptor+12,0x400);
        memory.write32(descriptor+16,priority);call.gpr[4]=descriptor;
        Require(kernel.dispatchThreadImport(4,call,0),"CreateThread fixture failed");
        const int id=int(call.gpr[2]);Require(id>0,"Thread stack allocation failed");
        call.gpr[4]=id;call.gpr[5]=0;
        Require(kernel.dispatchThreadImport(6,call,0),"StartThread fixture failed");
        return id;
    };
    auto take=[&](uint64_t cycle,int id) {
        auto* next=kernel.beginNextReady(cycle);
        Require(next && next->id==id,"Cached IOP election changed priority or wake timing");
        return next;
    };
    const auto low=create(40,0x50000),high=create(10,0x51000);
    auto* next=take(0,high);
    kernel.delayCurrentUntil(100,next->cpu);kernel.endTimeslice(*next,sentinel);
    for(unsigned i=0;i<100;++i){next=take(i,low);kernel.endTimeslice(*next,sentinel);}
    next=take(100,high);
    const auto flag=kernel.createInternalEventFlag(0,0,0);
    next->cpu.gpr[4]=flag;next->cpu.gpr[5]=1;next->cpu.gpr[6]=1;next->cpu.gpr[7]=0;
    Require(kernel.dispatchEventImport(10,next->cpu),"Event wait fixture failed");
    kernel.endTimeslice(*next,sentinel);
    next=take(101,low);kernel.endTimeslice(*next,sentinel);
    Require(kernel.setInternalEventFlag(flag,1),"External IOP event wake failed");
    next=take(102,high);kernel.endTimeslice(*next,sentinel);
    const auto urgent=create(5,0x52000);
    next=take(103,urgent);next->cpu.pc=sentinel;kernel.endTimeslice(*next,sentinel);
    next=take(104,high);kernel.endTimeslice(*next,sentinel);
    // Erase the cached candidate while it is ready, before the next election.
    call.gpr[4]=high;Require(kernel.dispatchThreadImport(5,call,105),"DeleteThread fixture failed");
    next=take(105,low);kernel.endTimeslice(*next,sentinel);
    const auto peer=create(40,0x53000);
    next=take(106,low);kernel.endTimeslice(*next,sentinel);
    kernel.terminateThreadsInRange(0x50000,4);kernel.cleanupDeadThreads();
    next=take(107,peer);kernel.endTimeslice(*next,sentinel);
    kernel.reset();Require(kernel.beginNextReady(108)==nullptr,"Reset retained a cached IOP thread");
    std::cout<<"IOP ready election: exact wake, event, creation, return, erase, tie, unload, reset: PASS\n";
}
}
#include "ps2_iop_audio_fixture.h"
#include "ps2_iop_timer_fixture.h"
#include "ps2_iop_mailbox_fixture.h"
#include "ps2_iop_rpc_fixture.h"
#include "ps2_iop_disc_fixture.h"
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--bench-idle") { IopIdleContract(true); return 0; }
        if(argc==2 && std::string_view(argv[1])=="--bench-dispatch") { IopDispatchBench(); return 0; }
        if(argc==2 && std::string_view(argv[1])=="--bench-clock") { IopClockBudgetContract(true); return 0; }
        if(argc==2 && std::string_view(argv[1])=="--bench-rpc-clock") { IopRpcClockContract(true); return 0; }
        AudioBridge();
        ReadyElectionInvalidation();
        IopMailboxContract();
        IopFixedPoolContract();
        IopThreadArgumentsContract();
        IopRpcClientContract();
        IopRpcClockContract();
        SysclibCharacterFlags();
        NopRetirement();
        AudioInsideGuestCall();
        IopImportLoadDelay();
        IopSifDmaCompletion();
        IopIsoSearchContract();
        AudioDmaCriticalSection();
        IopModuleArguments();
        IopIdleContract();
        IopScheduledTimerContract();
        IopClockBudgetContract();
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
