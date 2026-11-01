#include "runtime/gs/gs_frontend.h"
#include "gs_command_backend.h"
#include "runtime/ps2_memory.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool good,const char* text){if(!good)throw std::runtime_error(text);}
class Recorder final : public cave::ps2::GsCommandBackend {
public:
    size_t initialized{},resets{},snapshots{},writes{};
    std::vector<std::pair<GifPathId,std::vector<uint8_t>>> packets;
    std::vector<std::pair<uint8_t,uint64_t>> registers;
    std::vector<uint8_t> image;
    std::array<uint64_t,4> imageSetup{};
    std::vector<cave::ps2::GsFrame> frames;
    cave::ps2::GsCompletions completed;
    uint32_t acknowledged=0;
    cave::ps2::GsCompletions CollectCompletions() override{auto value=completed;completed={};return value;}
    void AcknowledgeStatus(uint32_t mask) override{acknowledged=mask;}
    void Initialize(std::span<const uint8_t> bytes) override{initialized=bytes.size();}
    void Reset() override{++resets;}
    void Gif(GifPathId path,std::span<const uint8_t> bytes) override{packets.emplace_back(path,std::vector<uint8_t>(bytes.begin(),bytes.end()));}
    void Register(uint8_t address,uint64_t value) override{registers.emplace_back(address,value);}
    void Image(const std::array<uint64_t,4>& setup,std::span<const uint8_t> bytes) override{imageSetup=setup;image.assign(bytes.begin(),bytes.end());}
    size_t ReadFifo(std::span<uint8_t> bytes) override{std::fill(bytes.begin(),bytes.end(),0x3c);return bytes.size();}
    void Snapshot(std::vector<uint8_t>& bytes) override{++snapshots;bytes.assign(4u*1024u*1024u,0xa5);}
    void WriteVram(size_t offset,std::span<const uint8_t> bytes) override{Require(offset==0 && bytes.size()==initialized,"Bad debug VRAM update");++writes;}
    void Frame(const cave::ps2::GsFrame& request) override{frames.push_back(request);}
};
void RawRouting() {
    std::vector<uint8_t> vram(4u*1024u*1024u);
    GSRegisters hardware{};
    hardware.vsyncTick=9;hardware.pmode=0x61;hardware.smode1=0x1234;hardware.syncv=0x5678;
    hardware.extbuf=0x9abc;hardware.extdata=0xdef0;hardware.extwrite=1;hardware.csr=0x2000;
    GS frontend;frontend.init(vram.data(),uint32_t(vram.size()),&hardware);
    auto backend=std::make_unique<Recorder>();auto* record=backend.get();
    frontend.setCommandBackend(std::move(backend));
    Require(frontend.usesCommandBackend() && record->initialized==vram.size(),"Raw backend did not receive initial VRAM");
    std::array<uint8_t,16> payload{};payload[9]=0x42;
    for(auto path:{GifPathId::Path1,GifPathId::Path2,GifPathId::Path3})frontend.processGIFPacket(path,payload.data(),16);
    Require(record->packets.size()==3 && record->packets[0].first==GifPathId::Path1 && record->packets[1].first==GifPathId::Path2 && record->packets[2].first==GifPathId::Path3,"Raw GIF path identity was lost");
    Require(record->packets[1].second[9]==0x42,"Raw GIF bytes were decoded or rewritten");
    frontend.writeRegister(GS_REG_RGBAQ,0x80563412);
    Require(record->registers.size()==1 && record->registers[0].second==0x80563412,"SDK direct register write was lost or duplicated");
    frontend.uploadImageNative(1,2,3,4,payload.data(),16);
    Require(record->imageSetup==std::array<uint64_t,4>{1,2,3,4} && record->image[9]==0x42,"Native image shortcut bypassed command routing");
    Require(frontend.processNativePackedGIFPacket(payload.data(),16) && record->packets.size()==4,"Packed shortcut bypassed raw routing");
    Require(!frontend.clearFramebufferContext(0,0) && !frontend.clearActiveFramebuffer(0) && record->registers.size()==1,"Native clear ran a second renderer");
    Require(std::all_of(vram.begin(),vram.end(),[](auto value){return value==0;}),"Raw commands also modified the CPU renderer");
    frontend.queueVBlankFrame();frontend.latchHostPresentationFrame();
    Require(record->frames.size()==1 && record->frames[0].vsyncTick==9 && record->frames[0].pmode==0x61,"Frame was not sampled on the command producer");
    const auto& frame=record->frames[0];
    Require(frame.smode1==0x1234 && frame.syncv==0x5678 && frame.extbuf==0x9abc && frame.extdata==0xdef0 && frame.extwrite==1 && frame.field==1,
            "Raw frame omitted privileged timing/circuit/field state");
    Require(record->snapshots==0,"Normal presentation read back GPU VRAM");
    frontend.writeRegister(0x59,0x2468);frontend.queueVBlankFrame();
    Require(record->frames.back().dispfb1==0x2468 && record->registers.size()==1,"SDK privileged pseudo-register entered drawing decoder or was lost");
    hardware.siglblid=0xaaaa0000bbbb0000ull;hardware.imr=0x100;
    record->completed={1,0x0000ffff00001234ull,0x0000ffff00005678ull,true,true,true};
    Require(frontend.pollCompletedEvents()==2 && hardware.siglblid==0xaaaa5678bbbb1234ull && (hardware.csr.load()&3)==3,
            "Completed GS events did not apply masked IDs/status/IRQ mask on frontend");
    Require(frontend.pollCompletedEvents()==0,"Completed IRQ delivered twice");
    hardware.imr=0;
    Require(frontend.afterPrivWrite(0x1010,0,0x100)==1,"Unmask did not raise pending signal IRQ");
    frontend.afterPrivWrite(0x1000,1,0);Require(record->acknowledged==1,"CSR acknowledgement did not reach owner mailbox");
    Require(frontend.consumeLocalToHostBytes(payload.data(),16)==16 && payload[0]==0x3c,"Game FIFO read did not reach command backend");
    Require(frontend.ReadVram(GS_PSM_CT32,0,1,2,3)==0xa5a5a5a5,"Debug pixel read used stale CPU memory");
    frontend.WriteVram(GS_PSM_CT32,0,1,2,3,0x12345678);
    Require(record->writes==1,"Explicit VRAM edit was lost");
    frontend.reset();Require(record->resets==1,"GS reset did not reach command owner");
    frontend.setCommandBackend(nullptr);
    Require(!frontend.usesCommandBackend() && vram[0]==0xa5,"Raw backend handoff discarded authoritative VRAM");
}
void ArbiterPaths() {
    std::vector<std::pair<GifPathId,uint64_t>> received;
    GifArbiter arbiter([&](GifPathId path,const uint8_t* data,uint32_t bytes){
        Require(bytes==16,"Arbiter changed payload size");uint64_t marker{};std::memcpy(&marker,data+8,8);received.emplace_back(path,marker);
    });
    auto submit=[&](GifPathId path,uint64_t marker,bool image=false,bool hl=false){
        const std::array<uint64_t,2> words{image?uint64_t(2)<<58:0,marker};
        arbiter.submit(path,reinterpret_cast<const uint8_t*>(words.data()),16,hl);
    };
    submit(GifPathId::Path3,1,true);submit(GifPathId::Path2,2,false,true);
    submit(GifPathId::Path2,3);submit(GifPathId::Path1,4);submit(GifPathId::Path3,5);
    submit(GifPathId::Path1,6);
    arbiter.drain();
    const std::vector<std::pair<GifPathId,uint64_t>> expected{{GifPathId::Path1,4},{GifPathId::Path1,6},{GifPathId::Path2,3},{GifPathId::Path3,1},{GifPathId::Path2,2},{GifPathId::Path3,5}};
    Require(received==expected && arbiter.empty(),"Arbitration lost paths, stable order or DIRECTHL exception");
}
}
int main(){try{RawRouting();ArbiterPaths();std::cout<<"GS raw routing, shortcuts, producer frame, no readback, handoff and arbitration: PASS\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
