#include "gs_device.h"
#include "gs_device_sink.h"
#include "gs_trace.h"
#include "runtime/gs/gs_types.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using namespace cave::ps2;
void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class T> std::span<const uint8_t> Bytes(const T& value){return {reinterpret_cast<const uint8_t*>(value.data()),sizeof(value)};}
}
int wmain(int argc,wchar_t** argv) {
    const bool keep=argc==3 && std::wstring(argv[1])==L"--keep";
    const auto path=keep?std::filesystem::path(argv[2]):std::filesystem::current_path()/("gpu-trace-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".evacgs");
    try {
        if(argc!=1 && !keep)throw std::invalid_argument("Usage: ps2_gs_trace_gpu_contract [--keep new-trace.evacgs]");
        std::vector<uint8_t> expected(GsDevice::VramBytes),actual(expected.size());GsTraceStats capture;
        {
            GsDevice device;
            auto sink=std::make_unique<GsDeviceSink>(device,[&](const GsFrame& f){Require(device.RenderField(f),"Authored GS field has no scanout");},[&]{device.WaitField();});
            GsRecordingBackend record(std::move(sink),path);
            record.Initialize(expected);record.Reset();
            record.Register(GS_REG_PRMODECONT,1);record.Register(GS_REG_XYOFFSET_1,0);
            record.Register(GS_REG_FRAME_1,uint64_t(10)<<16);record.Register(GS_REG_ZBUF_1,uint64_t(1)<<32);
            record.Register(GS_REG_TEST_1,0x30000);record.Register(GS_REG_SCISSOR_1,(uint64_t(639)<<16)|(uint64_t(223)<<48));
            record.Register(GS_REG_COLCLAMP,1);record.Register(GS_REG_PRIM,6|0x100);
            const std::array<uint64_t,2> tag{1u|(1u<<15)|(uint64_t(1)<<60),0xe};
            for(auto route:{GifPathId::Path1,GifPathId::Path2,GifPathId::Path3})record.Gif(route,Bytes(tag));
            // A direct SDK register between partial raw paths must retain that
            // identity, rather than being converted to another path's A_D tag.
            record.Register(GS_REG_XYOFFSET_1,0);
            for(unsigned i=0;i<3;++i) {
                const std::array<uint64_t,2> color{0x3f80000080604020ull+(uint64_t(i)<<8),GS_REG_RGBAQ};
                record.Gif(GifPathId(i+1),Bytes(color));
                record.Register(GS_REG_XYZ2,uint64_t(i*160*16));
                record.Register(GS_REG_XYZ2,uint64_t((i+1)*160*16)|(uint64_t(224*16)<<16));
            }
            const std::array<uint32_t,4> image{0x80332211,0x80665544,0x80998877,0x80ccbbaa};
            record.Image({(uint64_t(8192)<<32)|(uint64_t(1)<<48),0,uint64_t(2)|(uint64_t(2)<<32),0},Bytes(image));
            record.Register(GS_REG_BITBLTBUF,8192|(uint64_t(1)<<16));record.Register(GS_REG_TRXPOS,0);
            record.Register(GS_REG_TRXREG,uint64_t(2)|(uint64_t(2)<<32));record.Register(GS_REG_TRXDIR,1);
            std::array<uint8_t,16> reply{};Require(record.ReadFifo(reply)==16 && std::ranges::equal(reply,Bytes(image)),"Live FIFO differs from image upload");
            const std::array<uint8_t,16> edit{0x42,0x24,0x55};record.WriteVram(3u*1024u*1024u,edit);
            record.Register(GS_REG_SIGNAL,0xffffffff12345678ull);record.AcknowledgeSignal();
            record.Register(GS_REG_SIGNAL,0xffffffff87654321ull);record.AcknowledgeSignal();
            record.Register(GS_REG_FINISH,0);record.AcknowledgeStatus(2);
            GsFrame field{};field.pmode=1|(1u<<5)|(255u<<8);field.smode1=(uint64_t(32)<<3)|(uint64_t(2)<<13);
            field.dispfb1=uint64_t(10)<<9;
            field.display1=636|(uint64_t(25)<<12)|(uint64_t(3)<<23)|(uint64_t(2559)<<32)|(uint64_t(223)<<44);
            record.Frame(field);record.Finish();capture=record.Stats();device.WaitField();
            Require(!device.ReadbackRequests(),"Capture/field rendering took a VRAM snapshot");
            device.ReadbackVram(0,expected);
            uint32_t pixel{};std::memcpy(&pixel,expected.data()+GSPSMCT32::addrPSMCT32(0,10,0,0),4);
            Require(pixel==0x80604020,"Authored source sprite did not reach expected GS VRAM pixel");
        }
        {
            GsDevice device;device.SetOfflineReplay(true);
            GsDeviceSink sink(device,[&](const GsFrame& f){Require(device.RenderField(f),"Replayed GS field has no scanout");},[&]{device.WaitField();});
            GsTraceWorkload workload(path);const auto replay=workload.Replay(sink);device.WaitField();
            Require(replay.frames==1 && replay.commands==capture.commands && !device.ReadbackRequests(),"Replay lost field/commands or took a VRAM snapshot");
            device.ReadbackVram(0,actual);Require(actual==expected,"Captured GPU replay differs in 4 MiB VRAM");
            // Reuse the decoded graph after resetting GPU state. No file I/O or
            // payload allocation is required for another measured replay.
            device.Reset();workload.Replay(sink);device.WaitField();device.ReadbackVram(0,actual);
            Require(actual==expected,"Reusing a decoded GS workload changed VRAM");
        }
        if(!keep)std::filesystem::remove(path);
        std::cout<<"GS GPU capture/replay: 3 partial paths, shortcuts, FIFO, field scanout and 4 MiB exact match: PASS\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"; trace="<<path<<'\n';return 1;}
}
