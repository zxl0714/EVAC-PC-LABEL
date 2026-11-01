#include "gs_device.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include "runtime/gs/gs_types.h"
#include <array>
#include <atomic>
#include <cstring>
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
        gs.WriteVram(0,vram);
        gs.WriteRegister(GS_REG_PRMODECONT,1);
        gs.WriteRegister(GS_REG_FRAME_1,uint64_t(1)<<16);
        gs.WriteRegister(GS_REG_ZBUF_1,uint64_t(1)<<32);
        gs.WriteRegister(GS_REG_TEST_1,0x30000);
        gs.WriteRegister(GS_REG_SCISSOR_1,(uint64_t(63)<<16)|(uint64_t(63)<<48));
        gs.WriteRegister(GS_REG_XYOFFSET_1,0);
        gs.WriteRegister(GS_REG_COLCLAMP,1);
        gs.WriteRegister(GS_REG_PRIM,6|0x100);
        constexpr std::array paths{GsGifPath::Path1,GsGifPath::Path2,GsGifPath::Path3};
        constexpr std::array<uint32_t,3> colors{0x80563412,0x80785634,0x809a7856};
        // All paths retain separate partial tags. Original A_D register input
        // sets each sprite's color; it does not reconstruct decoded triangles.
        alignas(16) const std::array<uint64_t,2> tag{1u|(1u<<15)|(uint64_t(1)<<60),0xe};
        const auto bytes=[](const auto& words){
            return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(words.data()),sizeof(words));
        };
        for(const auto path:paths)gs.GifTransfer(path,bytes(tag));
        for(size_t i=0;i<paths.size();++i) {
            alignas(16) const std::array<uint64_t,2> ad{0x3f80000000000000ull|colors[i],GS_REG_RGBAQ};
            gs.GifTransfer(paths[i],bytes(ad));
            gs.WriteRegister(GS_REG_XYZ2,uint64_t(i*16*16));
            gs.WriteRegister(GS_REG_XYZ2,uint64_t((i+1)*16*16)|(uint64_t(16*16)<<16));
        }
        gs.Flush();
        gs.ReadbackVram(0,vram); // only this offscreen correctness test reads back
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            uint32_t value{};
            std::memcpy(&value,vram.data()+GSPSMCT32::addrPSMCT32(0,1,x,y),4);
            const auto expected=x<48 && y<16?colors[x/16]:0u;
            if(value!=expected)throw std::runtime_error("GPU GIF path/color/coverage differs from authored pixels");
        }
        std::atomic<bool> wrongOwnerRejected{};
        std::thread wrongOwner([&]{
            try{gs.WriteRegister(GS_REG_PRIM,0);}
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
        std::cout<<"GPU="<<gs.GpuName()<<"; raw GIF paths/fragments, 4096 pixels, owner/range checks: PASS\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
