#include "gs_device.h"
#include "gs_device_sink.h"
#include "gs_completion_mailbox.h"
#include "gs_command_queue.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include "runtime/gs/gs_types.h"
#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

int main() {
    try {
        using cave::ps2::GsDevice;
        using cave::ps2::GsGifPath;
        GsDevice gs;
        std::vector<uint8_t> vram(GsDevice::VramBytes);
        GS frontend;
        GSRegisters hardware{};
        unsigned presented=0;
        frontend.init(vram.data(),uint32_t(vram.size()),&hardware);
        frontend.setCommandBackend(std::make_unique<cave::ps2::GsDeviceSink>(gs,
            [&](const cave::ps2::GsFrame&){++presented;}));
        frontend.writeRegister(GS_REG_PRMODECONT,1);
        frontend.writeRegister(GS_REG_FRAME_1,uint64_t(1)<<16);
        frontend.writeRegister(GS_REG_ZBUF_1,uint64_t(1)<<32);
        frontend.writeRegister(GS_REG_TEST_1,0x30000);
        frontend.writeRegister(GS_REG_SCISSOR_1,(uint64_t(63)<<16)|(uint64_t(63)<<48));
        frontend.writeRegister(GS_REG_XYOFFSET_1,0);
        frontend.writeRegister(GS_REG_COLCLAMP,1);
        frontend.writeRegister(GS_REG_PRIM,6|0x100);
        constexpr std::array paths{GsGifPath::Path1,GsGifPath::Path2,GsGifPath::Path3};
        constexpr std::array<uint32_t,3> colors{0x80563412,0x80785634,0x809a7856};
        // All paths retain separate partial tags. Original A_D register input
        // sets each sprite's color; it does not reconstruct decoded triangles.
        alignas(16) const std::array<uint64_t,2> tag{1u|(1u<<15)|(uint64_t(1)<<60),0xe};
        const auto bytes=[](const auto& words){
            return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(words.data()),sizeof(words));
        };
        for(const auto path:paths)frontend.processGIFPacket(static_cast<GifPathId>(path),bytes(tag).data(),16);
        for(size_t i=0;i<paths.size();++i) {
            alignas(16) const std::array<uint64_t,2> ad{0x3f80000000000000ull|colors[i],GS_REG_RGBAQ};
            frontend.processGIFPacket(static_cast<GifPathId>(paths[i]),bytes(ad).data(),16);
            frontend.writeRegister(GS_REG_XYZ2,uint64_t(i*16*16));
            frontend.writeRegister(GS_REG_XYZ2,uint64_t((i+1)*16*16)|(uint64_t(16*16)<<16));
        }
        const auto readsBeforeFinish=gs.ReadbackRequests();
        frontend.writeRegister(GS_REG_FINISH,0);
        auto completion=gs.Completions()->Take();
        if(!completion.finish || completion.signal || presented || gs.ReadbackRequests()!=readsBeforeFinish)
            throw std::runtime_error("GPU FINISH required a frame or VRAM readback");
        std::vector<uint8_t> readback(vram.size());
        gs.ReadbackVram(0,readback); // diagnostic output is separate from the CPU mirror
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            uint32_t value{};
            std::memcpy(&value,readback.data()+GSPSMCT32::addrPSMCT32(0,1,x,y),4);
            const auto expected=x<48 && y<16?colors[x/16]:0u;
            if(value!=expected)throw std::runtime_error("GPU GIF path/color/coverage differs from authored pixels");
        }
        if(std::any_of(vram.begin(),vram.end(),[](auto value){return value!=0;}))
            throw std::runtime_error("GPU frontend also rendered into CPU VRAM");
        const std::array<uint32_t,4> pixels{0x80332211,0x80665544,0x80998877,0x80ccbbaa};
        const std::array<uint64_t,4> upload{(uint64_t(128)<<32)|(uint64_t(1)<<48),0,
                                         uint64_t(2)|(uint64_t(2)<<32),0};
        frontend.uploadImageNative(upload[0],upload[1],upload[2],upload[3],
            reinterpret_cast<const uint8_t*>(pixels.data()),16);
        frontend.writeRegister(GS_REG_BITBLTBUF,128|(uint64_t(1)<<16));
        frontend.writeRegister(GS_REG_TRXPOS,0);
        frontend.writeRegister(GS_REG_TRXREG,uint64_t(2)|(uint64_t(2)<<32));
        frontend.writeRegister(GS_REG_TRXDIR,1);
        std::array<uint32_t,4> downloaded{};
        if(frontend.consumeLocalToHostBytes(reinterpret_cast<uint8_t*>(downloaded.data()),16)!=16 || downloaded!=pixels)
            throw std::runtime_error("Native image upload / GPU FIFO round-trip changed pixels");
        frontend.queueVBlankFrame();
        if(presented!=1)throw std::runtime_error("GPU frame destination was bypassed");
        // Marker callbacks also have to work in the original raw A_D stream,
        // rather than only in the SDK's direct-register shortcut.
        const std::array<uint64_t,8> markers{
            3u|(1u<<15)|(uint64_t(1)<<60),0xe,
            0xffff000012340000ull,GS_REG_SIGNAL,
            0x0000ffff00005678ull,GS_REG_LABEL,
            0,GS_REG_FINISH};
        frontend.processGIFPacket(GifPathId::Path2,bytes(markers).data(),uint32_t(sizeof(markers)));
        completion=gs.Completions()->Take();
        if(!completion.signal || !completion.finish || !completion.label ||
           completion.signalPayload!=0xffff000012340000ull || completion.labelPayload!=0x0000ffff00005678ull)
            throw std::runtime_error("Raw GIF marker completion was lost");
        gs.Completions()->AcknowledgeSignal();
        std::atomic<bool> wrongOwnerRejected{};
        std::thread wrongOwner([&]{
            try{frontend.writeRegister(GS_REG_PRIM,0);}
            catch(const std::logic_error&){wrongOwnerRejected=true;}
        });
        wrongOwner.join();
        if(!wrongOwnerRejected)throw std::runtime_error("GS accepted state mutation from a non-owner thread");
        unsigned rejected=0;
        try{gs.GifTransfer(static_cast<GsGifPath>(0),{});}catch(const std::invalid_argument&){++rejected;}
        std::array<uint8_t,15> partial{};
        try{gs.GifTransfer(GsGifPath::Path1,partial);}catch(const std::invalid_argument&){++rejected;}
        try{gs.ReadbackVram(GsDevice::VramBytes,partial);}catch(const std::out_of_range&){++rejected;}
        if(rejected!=3)throw std::runtime_error("Invalid GS commands were accepted");
        {
            // Reset must also release a real GPU owner blocked by an unacked
            // second SIGNAL, while that reset command is queued behind it.
            using namespace std::chrono_literals;
            const auto mailbox=gs.Completions();const auto old=mailbox->Generation();
            cave::ps2::GsCommandQueue queue(8u*1024u*1024u,2048,mailbox);
            cave::ps2::GsDeviceSink sink(gs,[](const cave::ps2::GsFrame&){});
            std::promise<void> waiting;auto didWait=waiting.get_future();bool notified=false;
            const auto deadline=std::chrono::steady_clock::now()+3s;
            gs.SetWaitPump([&]{
                if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("SIGNAL reset handshake timed out");
                if(mailbox->Take().signal && !notified){notified=true;waiting.set_value();}
            });
            auto producer=std::async(std::launch::async,[&]{
                queue.Register(GS_REG_SIGNAL,0xffffffff00000001ull);
                queue.Register(GS_REG_SIGNAL,0xffffffff00000002ull);
                if(didWait.wait_for(3s)!=std::future_status::ready){queue.Close();throw std::runtime_error("GPU SIGNAL did not reach wait pump");}
                queue.Reset();
            });
            while(producer.wait_for(0ms)!=std::future_status::ready)queue.ConsumeOne(sink,5ms);
            producer.get();gs.SetWaitPump({});
            if(!notified || mailbox->Generation()==old || mailbox->Take() || queue.GetStats().commands)
                throw std::runtime_error("Queued RESET leaked stale SIGNAL completion");
        }
        std::cout<<"GPU="<<gs.GpuName()<<"; runtime GIF routing, 4096 pixels, native image/FIFO, owner/range checks: PASS\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
