#pragma once
#include "ps2_stubs.h"
#include "crt_mode.h"
namespace {
class GsSdkBackend final : public GsMmioBackend {
public:
    unsigned packets=0,registers=0;
    void Gif(GifPathId,std::span<const uint8_t>) override{++packets;}
    void Register(uint8_t,uint64_t) override{++registers;}
};
void GsSdkDisplay(PS2Runtime& runtime) {
    Require(runtime.syncCoreSubsystems(),"GS SDK headless binding failed");
    auto& mem=runtime.memory();auto& gs=runtime.gs();
    auto backend=std::make_unique<GsSdkBackend>();auto* captured=backend.get();
    gs.setCommandBackend(std::move(backend));
    auto* bytes=mem.getRDRAM();R5900Context ctx{};
    SET_GPR_U32(&ctx,29,0x80000);
    auto reset=[&](uint32_t mode,uint32_t interlace,uint32_t output,uint32_t frame){
        SET_GPR_U32(&ctx,4,mode);SET_GPR_U32(&ctx,5,interlace);
        SET_GPR_U32(&ctx,6,output);SET_GPR_U32(&ctx,7,frame);
        ps2_stubs::sceGsResetGraph(bytes,&ctx,&runtime);
        Require(Result(ctx)==0,"GS graph reset failed");
    };
    reset(0,1,2,0);
    Require(captured->resets==1 && !captured->packets && !captured->registers,
        "Privileged GS reset was encoded as drawing-register packets");
    Require(((mem.gs().smode1>>3)&127)==32 && ((mem.gs().smode1>>13)&3)==2 && mem.gs().smode2==1,
        "NTSC CRT clock/carrier/frame mode missing");
    ps2_stubs::sceGsGetGParam(bytes,&ctx,&runtime);
    const auto parameter=getRegU32(&ctx,2);std::array<uint16_t,4> values{};
    std::memcpy(values.data(),mem.getScratchpad()+(parameter&0x3fff),sizeof(values));
    Require(values[0]==1 && values[1]==2 && values[2]==0,"GS parameter ABI is not four shorts");
    auto make=[&](uint32_t width,uint32_t height,int32_t x,int32_t y){
        SET_GPR_U32(&ctx,4,0x6000);SET_GPR_U32(&ctx,5,0);
        SET_GPR_U32(&ctx,6,width);SET_GPR_U32(&ctx,7,height);
        SET_GPR_U32(&ctx,8,uint32_t(x));SET_GPR_U32(&ctx,9,uint32_t(y));
        // Register arguments that happen to be zero must not read these.
        const std::array<uint32_t,2> stale{123,456};std::memcpy(bytes+0x80010,stale.data(),sizeof(stale));
        ps2_stubs::sceGsSetDefDispEnv(bytes,&ctx,&runtime);
        Require(Result(ctx)==0,"GS display builder failed");
        std::array<uint64_t,5> result{};std::memcpy(result.data(),bytes+0x6000,sizeof(result));return result;
    };
    auto env=make(640,224,0,0);
    const uint64_t ntsc=636ull|(50ull<<12)|(3ull<<23)|(2559ull<<32)|(223ull<<44);
    Require(env[0]==0x66 && env[1]==1 && env[2]==(10ull<<9) && env[3]==ntsc,
        "GS display builder lost clock units, PMODE or zero EE register args");
    SET_GPR_U32(&ctx,4,0x6000);ps2_stubs::sceGsPutDispEnv(bytes,&ctx,&runtime);
    Require(mem.gs().pmode==0x66 && mem.gs().display1==ntsc && mem.gs().display2==ntsc,
        "SDK display environment did not reach privileged registers");
    Require(!captured->packets && !captured->registers,"Display MMIO touched drawing registers");
    reset(5,0,3,0);env=make(320,256,-2,3);
    Require(captured->resets==1 && ((mem.gs().smode1>>13)&3)==3,
        "CRT-only mode reset the drawing state or missed PAL carrier");
    Require(env[1]==2 && (env[3]&4095)==640 && ((env[3]>>12)&2047)==39 &&
        ((env[3]>>23)&15)==7 && ((env[3]>>32)&4095)==2559,"PAL display offsets/magnification wrong");
    reset(5,1,2,1);env=make(640,224,0,0);
    Require(env[1]==3 && ((env[3]>>44)&2047)==447,"Frame mode omitted alternate field height");
    const auto before=mem.gs().smode1;
    bool rejected=false;try{reset(5,0,0xff,0);}catch(const std::invalid_argument&){rejected=true;}
    Require(rejected && mem.gs().smode1==before,"Unsupported CRT mode partially changed state");
    // GsPutIMR must use the same MMIO notification as a guest store.
    mem.gs().imr=0x300;mem.gs().csr=0;captured->completed={1,0xffffffff12345678ull,0,true,true,false};
    Require((mem.read64(0x12001000)&1)!=0,"Pending signal was not collected");
    unsigned callbacks=0;mem.setGsAccessCallback([&](uint32_t offset,uint64_t written,uint64_t old,bool after){
        if(after){++callbacks;gs.afterPrivWrite(offset,written,old);}else gs.pollCompletedEvents();
    });
    SET_GPR_U64(&ctx,4,0x200);ps2_syscalls::GsPutIMR(bytes,&ctx,&runtime);
    Require(callbacks==1 && mem.gs().imr==0x200,"SDK interrupt unmask bypassed MMIO notification");
    mem.setGsAccessCallback({});gs.setCommandBackend(nullptr);
}
}
