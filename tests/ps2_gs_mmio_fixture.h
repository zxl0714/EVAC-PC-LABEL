#pragma once
#include "gs_command_backend.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include <mutex>

namespace {
class GsMmioBackend final : public cave::ps2::GsCommandBackend {
public:
    cave::ps2::GsCompletions completed;
    uint32_t acknowledged=0,resets=0;
    std::mutex mutex;
    void Publish(cave::ps2::GsCompletions value){std::lock_guard lock(mutex);completed=value;}
    void Initialize(std::span<const uint8_t>) override{}
    void Reset() override{std::lock_guard lock(mutex);++resets;completed={};}
    void Gif(GifPathId,std::span<const uint8_t>) override{}
    void Register(uint8_t,uint64_t) override{}
    void Image(const std::array<uint64_t,4>&,std::span<const uint8_t>) override{}
    size_t ReadFifo(std::span<uint8_t>) override{return 0;}
    void Snapshot(std::vector<uint8_t>& bytes) override{bytes.assign(PS2_GS_VRAM_SIZE,0);}
    void WriteVram(size_t,std::span<const uint8_t>) override{}
    void Frame(const cave::ps2::GsFrame&) override{}
    cave::ps2::GsCompletions CollectCompletions() override{std::lock_guard lock(mutex);auto result=completed;completed={};return result;}
    void AcknowledgeStatus(uint32_t mask) override{acknowledged=mask;}
};
void GsMmioCompletions(PS2Memory& memory) {
    constexpr uint32_t csr=0x12001000,imr=0x12001010,ids=0x12001080;
    GS frontend;frontend.init(memory.getGSVRAM(),uint32_t(PS2_GS_VRAM_SIZE),&memory.gs());
    auto backend=std::make_unique<GsMmioBackend>();auto* record=backend.get();
    frontend.setCommandBackend(std::move(backend));
    uint32_t interrupts=0;
    memory.setGsAccessCallback([&](uint32_t offset,uint64_t value,uint64_t previous,bool afterWrite){
        interrupts|=afterWrite?frontend.afterPrivWrite(offset,value,previous):frontend.pollCompletedEvents();
    });
    memory.gs().csr=0;memory.gs().siglblid=0xaaaa0000bbbb0000ull;memory.gs().imr=0x300;
    record->completed={1,0x0000ffff00001234ull,0x0000ffff00005678ull,true,true,true};
    Require((memory.read64(csr)&3)==3 && !interrupts,"GS MMIO polling failed to receive masked completions");
    Require(memory.read64(ids)==0xaaaa5678bbbb1234ull && memory.read8(ids)==0x34 && memory.read16(ids+4)==0x5678,
            "GS masked IDs differ between byte/half/dword ports");
    memory.write32(imr,0x200);Require(interrupts==1,"IMR unmask did not notify pending GS signal");
    interrupts=0;memory.write32(imr,0x200);Require(!interrupts,"Unchanged IMR duplicated GS IRQ");
    memory.write8(csr,1);
    Require((memory.read32(csr)&3)==2 && record->acknowledged==1,"Byte CSR acknowledgement cleared wrong status");
    memory.write32(csr+4,1);
    Require((memory.read32(csr)&3)==2 && record->acknowledged==0,"CSR high dword acknowledged low status");
    memory.write16(csr,2);
    Require(!(memory.read64(csr)&3) && record->acknowledged==2,"Halfword FINISH acknowledgement failed");
    record->completed={1,0xffffffff87654321ull,0,true,true,false};
    memory.write64(csr,0x200);
    Require(record->resets==1 && !(memory.read64(csr)&3) && memory.read64(ids)==0,"GS CSR RESET retained stale completed events");
    memory.setGsAccessCallback({});
    frontend.setCommandBackend(nullptr);
}
}
