// Authored integration workload: no ROM, BIOS, generated game code or save data.
#include "native_host.h"
#include "ps2_runtime.h"
#include "runtime/gs/gs_types.h"
#include "crt_mode.h"
#include <chrono>
#include <iostream>
#include <thread>

namespace {
unsigned drawn=0;bool fault=false,hold=false;
void Draw(uint8_t*,R5900Context* ctx,PS2Runtime* runtime) {
    if(fault && drawn==3)throw std::runtime_error("authored EE failure");
    auto& memory=runtime->memory();auto& gs=runtime->gs();
    if(!drawn) {
        memory.write64(0x12000010,cave::ps2::CrtSmode1(2));
        memory.write64(0x12000020,0);
        const auto display=cave::ps2::MakeSdkDisplay(2,false,false,0,640,224);
        memory.write64(0x12000000,1|(1u<<5)|(255u<<8));
        memory.write64(0x12000070,display.dispfb);
        memory.write64(0x12000080,display.display);
        gs.writeRegister(GS_REG_PRMODECONT,1);gs.writeRegister(GS_REG_XYOFFSET_1,0);
        gs.writeRegister(GS_REG_FRAME_1,uint64_t(10)<<16);
        gs.writeRegister(GS_REG_ZBUF_1,uint64_t(1)<<32);gs.writeRegister(GS_REG_TEST_1,0x30000);
        gs.writeRegister(GS_REG_SCISSOR_1,(uint64_t(639)<<16)|(uint64_t(223)<<48));
        gs.writeRegister(GS_REG_COLCLAMP,1);gs.writeRegister(GS_REG_PRIM,6|0x100);
    }
    gs.writeRegister(GS_REG_RGBAQ,0x3f80000080604020ull);
    gs.writeRegister(GS_REG_XYZ2,0);gs.writeRegister(GS_REG_XYZ2,uint64_t(640*16)|(uint64_t(224*16)<<16));
    gs.queueVBlankFrame();++drawn;
    if(hold)std::this_thread::sleep_for(std::chrono::milliseconds(16));
    ctx->pc=ctx->pc==0x100000?0x100004:0x100000;
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2){fault=std::string_view(argv[1])=="--fault";hold=std::string_view(argv[1])=="--hold";}
        auto runtime=std::make_unique<PS2Runtime>();
        runtime->nativeHost().Configure({640,480});
        unsigned presented=0;
        runtime->setDebugUiCallbacks([](PS2Runtime&,void*){},[](PS2Runtime& state,void* user){
            auto& presented=*static_cast<unsigned*>(user);
            if(++presented>=(hold?3600:10))state.requestStop();
        },[](PS2Runtime&,void*){},&presented);
        if(!runtime->initialize("EVAC PC LABEL | PS2 native host contract"))
            throw std::runtime_error("Native host initialization failed");
        runtime->registerFunction(0x100000,Draw);runtime->registerFunction(0x100004,Draw);
        runtime->cpu().pc=0x100000;
        try {runtime->run();}
        catch(const std::runtime_error& error) {
            if(fault && std::string_view(error.what())=="authored EE failure") {
                std::cout<<"Native host: EE failure propagated after queue/thread cleanup: PASS\n";return 0;
            }
            throw;
        }
        if(fault || !presented || (!hold && presented!=10))throw std::runtime_error("Authored host frame count differs");
        std::cout<<"Native host: frames="<<presented<<" clean_shutdown=1: PASS\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
