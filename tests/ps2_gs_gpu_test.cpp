#include "gs_device.h"
#include "field_blit.h"
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

namespace {
using cave::ps2::GsDevice;
using cave::ps2::GsGifPath;

template<class T> std::span<const uint8_t> asBytes(const T& value) {
    return {reinterpret_cast<const uint8_t*>(&value), sizeof(value)};
}

void checkDrawingState(GsDevice& source) {
    source.Reset();
    constexpr uint32_t palettePixel = 0x80945b31u;
    constexpr uint32_t overwrittenPixel = 0x8025a0f2u;
    constexpr uint32_t paletteBlock = 384, textureBlock = 320;
    source.WriteVram(paletteBlock * 256, asBytes(palettePixel));
    source.WriteVram(textureBlock * 256, asBytes(uint32_t{}));
    source.WriteRegister(GS_REG_PRMODECONT, 1);
    source.WriteRegister(GS_REG_FRAME_1, 1u << 16);
    source.WriteRegister(GS_REG_ZBUF_1, 1ull << 32);
    source.WriteRegister(GS_REG_SCISSOR_1, (63ull << 16) | (63ull << 48));
    source.WriteRegister(GS_REG_TEST_1, 0x30000);
    source.WriteRegister(GS_REG_XYOFFSET_1, 32 | (48ull << 32));
    source.WriteRegister(GS_REG_COLCLAMP, 1);
    source.WriteRegister(GS_REG_PRIM, 6 | (1 << 4) | (1 << 8));
    // One PSMT8H texel with index 0, DECAL, and a palette loaded once.
    const uint64_t tex0 = textureBlock | (1ull << 14) | (27ull << 20) |
        (1ull << 34) | (1ull << 35) | (uint64_t(paletteBlock) << 37) | (1ull << 61);
    source.WriteRegister(GS_REG_TEX0_1, tex0);
    source.Flush();
    source.WriteVram(paletteBlock * 256, asBytes(overwrittenPixel));
    source.WriteRegister(GS_REG_UV, 0);
    source.WriteRegister(GS_REG_XYZ2, 32 | (48ull << 16));

    // The FIFO already owns an independent copy, even if the source changes.
    constexpr std::array<uint32_t, 8> pixels{11, 12, 13, 14, 21, 22, 23, 24};
    constexpr uint64_t rect = 8 | (1ull << 32);
    source.UploadImage({(128ull << 32) | (1ull << 48), 0, rect, 0}, asBytes(pixels));
    source.WriteRegister(GS_REG_BITBLTBUF, 128 | (1ull << 16));
    source.WriteRegister(GS_REG_TRXREG, rect);
    source.WriteRegister(GS_REG_TRXDIR, 1);
    std::array<uint8_t, 16> first{};
    source.ReadTransferFifo(first);
    source.WriteRegister(GS_REG_BITBLTBUF, (256ull << 32) | (1ull << 48));
    source.WriteRegister(GS_REG_TRXDIR, 0);
    source.WriteRegister(GS_REG_HWREG, 0x1212121211111111ull);

    alignas(16) const std::array<uint64_t, 2> adTag{1 | (1ull << 15) | (1ull << 60), 0xe};
    for (auto path : {GsGifPath::Path1, GsGifPath::Path3}) source.GifTransfer(path, asBytes(adTag));
    auto saved = source.SaveDrawingState();
    auto finish = [&](GsDevice& gs) {
        std::array<uint8_t, 16> remaining{};
        gs.ReadTransferFifo(remaining);
        if (std::memcmp(remaining.data(), pixels.data() + 4, 16))
            throw std::runtime_error("Saved GS read FIFO did not resume at its cursor");
        for (uint64_t word : {0x1414141413131313ull, 0x1616161615151515ull, 0x1818181817171717ull})
            gs.WriteRegister(GS_REG_HWREG, word);
        alignas(16) const std::array<uint64_t, 2> uv{0, GS_REG_UV};
        alignas(16) const std::array<uint64_t, 2> xyz{(32 + 16*16) | (uint64_t(48 + 16*16) << 16), GS_REG_XYZ2};
        gs.GifTransfer(GsGifPath::Path1, asBytes(uv));
        gs.GifTransfer(GsGifPath::Path3, asBytes(xyz));
        gs.Flush();
        std::vector<uint8_t> vram(GsDevice::VramBytes);
        gs.ReadbackVram(0, vram);
        uint32_t drawn{}, lastUpload{};
        std::memcpy(&drawn, vram.data() + GSPSMCT32::addrPSMCT32(0,1,12,12), sizeof(drawn));
        std::memcpy(&lastUpload, vram.data() + GSPSMCT32::addrPSMCT32(256,1,7,0), sizeof(lastUpload));
        if (drawn != palettePixel) throw std::runtime_error("Mid-sprite snapshot lost the latched GS palette or geometry");
        if (lastUpload != 0x18181818u) throw std::runtime_error("Partial GS image upload did not finish");
        return vram;
    };
    const auto expected = finish(source);
    GsDevice restored;
    for (int pass = 0; pass < 2; ++pass) {
        restored.LoadDrawingState(saved);
        try {
            restored.LoadDrawingState(std::span(saved).first(saved.size() - 1));
            throw std::runtime_error("Truncated GS drawing state was accepted");
        } catch (const std::invalid_argument&) {}
        if (finish(restored) != expected) throw std::runtime_error("Restored GS VRAM differs from uninterrupted drawing");
    }
}
}

int main() {
    try {
        using cave::ps2::GsDevice;
        using cave::ps2::GsGifPath;
        // Repeat the same game field on consecutive vblanks. The GS draw
        // geometry, rather than the presentation counter, identifies its rows.
        std::array<uint64_t,8> quad{0,0,320u<<16,224u<<20,(320u<<16)|240u,(224u<<20)|336u,240,336};
        const std::span blitBytes(reinterpret_cast<const uint8_t*>(quad.data()),sizeof(quad));
        for(unsigned phase:{1u,1u,0u,0u,1u}) {
            for(unsigned corner=0;corner<4;++corner) {
                auto& xyz=quad[2*corner+1];
                xyz=(xyz&~(8ull<<16))|((phase?0ull:8ull)<<16);
            }
            if(cave::ps2::FieldBlitPhase(blitBytes,0,0,224)!=phase)
                throw std::runtime_error("Half-line display blit lost its source phase");
        }
        if(cave::ps2::FieldBlitPhase(blitBytes.subspan(16),0,0,224) ||
           cave::ps2::FieldBlitPhase(blitBytes,0,0,200))
            throw std::runtime_error("Partial blit classified as a display field");
        // SLPM-66301, stage 1, GS XYOFFSET (0x6c00, 0x7900).
        quad={0,0xffff79007570,0x14000000,0xffff86f87570,
              0x14000f00,0xffff86f88a70,0xf00,0xffff79008a70};
        if(cave::ps2::FieldBlitPhase(blitBytes,0x6c00,0x7900,224)!=1)
            throw std::runtime_error("Truncated field blit lost its source phase");
        for(unsigned i=1;i<quad.size();i+=2)quad[i]+=8ull<<16;
        if(cave::ps2::FieldBlitPhase(blitBytes,0x6c00,0x7900,224)!=0)
            throw std::runtime_error("Shifted truncated field blit lost its source phase");
        for(unsigned i:{3u,5u})quad[i]-=8ull<<16;
        if(cave::ps2::FieldBlitPhase(blitBytes,0x6c00,0x7900,224))
            throw std::runtime_error("A missing field row was accepted as complete");
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
        checkDrawingState(gs);
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
        std::cout<<"GPU="<<gs.GpuName()<<"; runtime GIF routing, native image/FIFO, cold drawing-state restore: PASS\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
