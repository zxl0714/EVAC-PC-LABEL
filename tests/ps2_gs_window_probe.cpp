#include "gs_display.h"
#include "gs_device_sink.h"
#include "gs_command_queue.h"
#include "runtime/gs/gs_types.h"
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc,char** argv) {
    try {
        using namespace std::chrono_literals;
        const unsigned limit=argc>=2?unsigned(std::stoul(argv[1])):120;
        if(!limit || limit>3600)throw std::invalid_argument("Frame limit must be 1..3600");
        const bool vsync=argc>=3 && std::string(argv[2])=="fifo";
        const auto transfer=argc>=4 && std::string(argv[3])=="shader"?
            cave::ps2::GsPresentTransfer::Sampled:cave::ps2::GsPresentTransfer::Automatic;
        cave::ps2::GsDisplay display(640,480,L"EVAC PC LABEL | PS2 graphics",vsync,transfer);
        auto& gs=display.Device();
        cave::ps2::GsCommandQueue queue;
        unsigned frames=0;
        bool closed=false;
        cave::ps2::GsDeviceSink sink(gs,[&](const cave::ps2::GsFrame& request){
            if(!display.Present(request)){closed=true;return;}
            ++frames;
        },[&]{display.WaitPreviousFrame();});
        auto producer=std::async(std::launch::async,[&]{
            std::vector<uint8_t> empty(cave::ps2::GsDevice::VramBytes);
            queue.Initialize(empty);
            queue.Register(GS_REG_PRMODECONT,1);queue.Register(GS_REG_XYOFFSET_1,0);
            queue.Register(GS_REG_FRAME_1,uint64_t(10)<<16);
            queue.Register(GS_REG_ZBUF_1,uint64_t(1)<<32);
            queue.Register(GS_REG_TEST_1,0x30000);
            queue.Register(GS_REG_SCISSOR_1,(uint64_t(639)<<16)|(uint64_t(223)<<48));
            queue.Register(GS_REG_COLCLAMP,1);queue.Register(GS_REG_PRIM,6|0x100);
            cave::ps2::GsFrame request{};
            request.smode1=(uint64_t(32)<<3)|(uint64_t(2)<<13);
            request.pmode=1|(1u<<5)|(255u<<8);request.dispfb1=uint64_t(10)<<9;
            // Authored NTSC 640x224 circuit including its active-area origin.
            request.display1=636|(uint64_t(25)<<12)|(uint64_t(3)<<23)|
                             (uint64_t(2559)<<32)|(uint64_t(223)<<44);
            const auto start=std::chrono::steady_clock::now();
            for(unsigned field=0;field<limit;++field) {
                queue.Register(GS_REG_RGBAQ,0x3f800000ff604020ull);
                queue.Register(GS_REG_XYZ2,0);
                queue.Register(GS_REG_XYZ2,uint64_t(640*16)|(uint64_t(224*16)<<16));
                request.vsyncTick=field;queue.Frame(request);
                // Authored test cadence only; real game timing stays with EE.
                std::this_thread::sleep_until(start+(field+1)*16ms);
            }
        });
        struct CloseOnExit {cave::ps2::GsCommandQueue& queue;~CloseOnExit(){queue.Close();}} close{queue};
        while(!closed && display.Alive() && producer.wait_for(0ms)!=std::future_status::ready)
            queue.ConsumeOne(sink,5ms);
        if(closed || !display.Alive()){
            queue.Close();try{producer.get();}catch(...){}
            std::cout<<"window_closed=1; presented="<<frames<<"; readbacks="<<gs.ReadbackRequests()<<'\n';return 2;
        }
        producer.get();
        if(gs.ReadbackRequests())throw std::runtime_error("Presentation read back GPU VRAM");
        const auto stats=queue.GetStats();
        if(stats.commands || stats.frameOutstanding)throw std::runtime_error("Graphics queue did not drain");
        std::cout<<"GPU="<<gs.GpuName()<<"; presented="<<frames<<"; readbacks="<<gs.ReadbackRequests()
                 <<"; queue_high_water_bytes="<<stats.highWaterBytes
                 <<"; queue_high_water_commands="<<stats.highWaterCommands<<'\n';
        return frames==limit?0:2;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
