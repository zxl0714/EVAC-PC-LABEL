#include "spu_device.h"
#include "pcm_queue.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

using cave::ps2::SpuDevice;
namespace {
void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
uint32_t Address(unsigned core,uint32_t offset) {return 0x1f900000u+core*0x400u+offset;}
void Write(SpuDevice& spu,unsigned core,uint32_t offset,uint16_t value) {
    spu.Write16(Address(core,offset),value);
}
void SetAddress(SpuDevice& spu,unsigned core,uint32_t reg,uint32_t address) {
    Write(spu,core,reg,uint16_t(address>>17));
    Write(spu,core,reg+2,uint16_t(address>>1));
}
void TransferAll(SpuDevice& spu,unsigned core,std::span<uint8_t> data,bool toSpu) {
    size_t offset=0;
    while(offset<data.size()) {
        const auto done=spu.Transfer(core,data.subspan(offset),toSpu);
        Require(done>0 && done<=data.size()-offset,"DMA made no progress or exceeded its span");
        offset+=done;
    }
}
void DmaRoundTrip() {
    SpuDevice spu;
    for(unsigned core=0;core<2;++core) {
        // Larger than either upstream DMA throttle, and crosses the physical
        // end of sound RAM. Each transfer still uses aligned 32-bit words.
        std::vector<uint8_t> sent(4096),received(sent.size());
        for(size_t i=0;i<sent.size();++i)sent[i]=uint8_t((i*17u+core*31u)&255u);
        Write(spu,core,0x19a,0x8020);
        SetAddress(spu,core,0x1a8,2u*1024u*1024u-16u);
        TransferAll(spu,core,sent,true);
        Write(spu,core,0x19a,0x8030);
        SetAddress(spu,core,0x1a8,2u*1024u*1024u-16u);
        TransferAll(spu,core,received,false);
        Require(sent==received,"SPU2 DMA data differed across RAM wrap/throttling");
    }
}
void VoicesAndReset() {
    SpuDevice spu;
    std::array<int16_t,512> pcm{};
    std::vector<uint8_t> sample(16,0x77);
    sample[0]=4;sample[1]=3; // Authored looping ADPCM, no game data.
    unsigned voices=0;
    for(unsigned core=0;core<2;++core)for(unsigned voice=0;voice<24;++voice) {
        spu.Reset();
        Write(spu,core,0x19a,0x8020);
        SetAddress(spu,core,0x1a8,0x5000);
        TransferAll(spu,core,sample,true);
        Write(spu,core,voice*16+0,0x3fff);
        Write(spu,core,voice*16+2,0x3fff);
        Write(spu,core,voice*16+4,0x1000);
        Write(spu,core,voice*16+6,0x000f);
        Write(spu,core,voice*16+8,0x1fc0);
        SetAddress(spu,core,0x1c0+voice*12,0x5000);
        SetAddress(spu,core,0x1c4+voice*12,0x5000);
        Write(spu,core,0x1a0+(voice/16)*2,uint16_t(1u<<(voice%16)));
        unsigned active=0;
        for(unsigned block=0;block<16;++block) {
            spu.Render(pcm);
            active+=std::any_of(pcm.begin(),pcm.end(),[](int16_t value){return value!=0;});
        }
        Require(active>=15,"A core/voice failed sustained loop playback");
        Write(spu,core,0x1a4+(voice/16)*2,uint16_t(1u<<(voice%16)));
        for(unsigned block=0;block<16;++block)spu.Render(pcm);
        Require(std::all_of(pcm.begin(),pcm.end(),[](int16_t value){return value==0;}),
                "Key-off failed to finish release");
        ++voices;
    }
    spu.Reset();spu.Render(pcm);
    Require(!spu.IrqPending(),"Reset retained a pending sound interrupt");
    Require(std::all_of(pcm.begin(),pcm.end(),[](int16_t value){return value==0;}),
            "Reset retained active voices");
    std::cout<<"sustained_voices="<<voices<<" DMA both cores/wrap/throttle, release, reset: PASS\n";
}
void CrossCoreIrq() {
    SpuDevice spu;
    std::vector<uint8_t> sample(16);
    sample[1]=7;
    Write(spu,0,0x19a,0x8020);
    SetAddress(spu,0,0x1a8,0x6000);
    TransferAll(spu,0,sample,true);
    Write(spu,0,4,0x1000);
    SetAddress(spu,0,0x1c0,0x6000);
    SetAddress(spu,1,0x19c,0x6000);
    Write(spu,1,0x19a,0x8040);
    Write(spu,0,0x1a0,1);
    std::array<int16_t,512> pcm{};
    spu.Render(pcm);spu.Render(pcm);
    Require(spu.IrqPending() && (spu.Read16(0x1f9007c2)&8u),
            "Core0 sample fetch failed to trigger Core1 IRQ address");
    Write(spu,1,0x19a,0x8000);
    spu.Render(pcm);
    Require(!spu.IrqPending() && !(spu.Read16(0x1f9007c2)&8u),
            "Clearing IRQ enable failed to acknowledge Core1 IRQ");
    std::cout<<"cross-core sound IRQ and acknowledgement: PASS\n";
}
void AutoDmaBackpressure() {
    for(unsigned core=0;core<2;++core) {
        SpuDevice spu;
        std::vector<uint8_t> data(2048,0x24);
        Write(spu,core,0x19a,0x8000);
        Write(spu,core,0x1b0,uint16_t(core+1));
        Require(spu.Transfer(core,data,true)==1024,"AutoDMA did not limit input to sound buffer capacity");
        Require(spu.Transfer(core,data,true)==0,"AutoDMA overwrote unconsumed input");
        std::array<int16_t,512> pcm{};
        spu.Render(pcm);
        Require(std::any_of(pcm.begin(),pcm.end(),[](int16_t value){return value!=0;}),
                "AutoDMA input failed to reach PCM output");
        Require(spu.Transfer(core,data,true)==1024,"AutoDMA failed to resume after consumption");
    }
    std::cout<<"AutoDMA input backpressure and resume, both cores: PASS\n";
}
void Benchmark() {
    std::cout<<"voices,frames,elapsed_ms,peak,checksum\n";
    for(const unsigned voices:{1u,24u,48u}) {
        SpuDevice spu;
        std::vector<uint8_t> sample(16,0x77);sample[0]=4;sample[1]=3;
        Write(spu,0,0x19a,0x8020);SetAddress(spu,0,0x1a8,0x5000);
        TransferAll(spu,0,sample,true);
        for(unsigned index=0;index<voices;++index) {
            const auto core=index/24,voice=index%24;
            Write(spu,core,0x19a,0x8000);
            Write(spu,core,voice*16,0x180);Write(spu,core,voice*16+2,0x180);
            Write(spu,core,voice*16+4,0x1000);
            Write(spu,core,voice*16+6,0x000f);Write(spu,core,voice*16+8,0x1fc0);
            SetAddress(spu,core,0x1c0+voice*12,0x5000);
            SetAddress(spu,core,0x1c4+voice*12,0x5000);
            Write(spu,core,0x1a0+(voice/16)*2,uint16_t(1u<<(voice%16)));
        }
        std::array<int16_t,512> pcm{};
        int64_t checksum=0;int peak=0;
        const auto start=std::chrono::steady_clock::now();
        for(unsigned block=0;block<1875;++block) {
            spu.Render(pcm);
            for(const auto value:pcm){checksum+=value;peak=std::max(peak,std::abs(int(value)));}
        }
        const auto elapsed=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count();
        Require(peak>0,"Sound benchmark unexpectedly produced silence");
        std::cout<<voices<<",480000,"<<elapsed<<','<<peak<<','<<checksum<<'\n';
    }
}
void PcmQueueContract() {
    cave::ps2::PcmQueue queue;
    std::vector<int16_t> full(queue.CapacityFrames*2,0x1234),output(full.size());
    Require(queue.Write(full)==queue.CapacityFrames,"PCM queue did not accept its exact capacity");
    Require(queue.Write(full)==0,"PCM queue overwrote unread audio");
    queue.DiscardFromProducer();
    Require(queue.Read(output)==0,"PCM queue retained audio from before IOP reset");
    Require(queue.Write(full)==queue.CapacityFrames,"PCM queue did not recover after discard");
    Require(queue.Read(output)==queue.CapacityFrames && output==full,"PCM queue changed stereo samples");

    constexpr uint32_t frames=300000;
    std::atomic<bool> failed{};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    std::thread producer([&] {
        std::array<int16_t,66> packet{};
        uint32_t sent=0;
        while(sent<frames && !failed.load()) {
            const auto count=std::min<uint32_t>(33,frames-sent);
            for(uint32_t i=0;i<count;++i) {
                packet[i*2]=int16_t((sent+i)%16000+1);
                packet[i*2+1]=int16_t(-packet[i*2]);
            }
            sent+=uint32_t(queue.Write(std::span(packet).first(count*2)));
            if(std::chrono::steady_clock::now()>deadline)failed=true;
        }
    });
    std::array<int16_t,34> received{};
    uint32_t read=0;
    while(read<frames && !failed.load()) {
        const auto count=queue.Read(received);
        for(size_t i=0;i<count;++i) {
            const auto expected=int16_t((read+i)%16000+1);
            if(received[i*2]!=expected || received[i*2+1]!=-expected)failed=true;
        }
        read+=uint32_t(count);
        if(std::chrono::steady_clock::now()>deadline)failed=true;
    }
    producer.join();
    Require(!failed && read==frames,"PCM producer/consumer lost or reordered stereo frames");
    std::cout<<"PCM queue concurrent wrap, backpressure and IOP-reset discard: PASS\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--bench")Benchmark();
        else if(argc==1){DmaRoundTrip();VoicesAndReset();CrossCoreIrq();AutoDmaBackpressure();PcmQueueContract();}
        else throw std::invalid_argument("Expected no argument, or --bench");
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
