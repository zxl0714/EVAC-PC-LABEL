#pragma once
#include "emulator/iop_emulator.h"
#include <array>
#include <cstring>
#include <vector>
#include <chrono>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace {
// Only the authored ELF image is visible to this host. No game/BIOS files.
class AudioFixtureHost final : public ps2x::iop::IopHost {
public:
    std::vector<uint8_t> image;
    bool readGuest(uint32_t a,void* out,size_t n) const override {
        if(a>image.size() || n>image.size()-a)return false;
        if(n)std::memcpy(out,image.data()+a,n);
        return true;
    }
    bool writeGuest(uint32_t,const void*,size_t) override{return false;}
    bool zeroGuest(uint32_t,size_t) override{return false;}
    bool normalizeGuestAddress(uint32_t a,uint32_t& n) const override{n=a;return a<image.size();}
    uint32_t allocateIopHandle(ps2x::iop::IopHandleKind) override{return 0;}
    uint32_t allocateGuest(uint32_t,uint32_t) override{return 0;}
    void freeGuest(uint32_t) override{}
    void audioCommand(uint32_t,uint32_t,ps2x::iop::GuestBuffer,ps2x::iop::GuestBuffer) override{}
    std::string hostPath(ps2x::iop::HostPathKind) const override{return {};}
    std::string translateGuestPath(std::string_view p) const override{return std::string(p);}
    uint64_t openHostFile(std::string_view) override{return 0;}
    bool hostFileSize(uint64_t,uint64_t&) const override{return false;}
    bool readHostFile(uint64_t,uint64_t,void*,size_t,size_t&) override{return false;}
    void closeHostFile(uint64_t) override{}
    int32_t memoryCard(const ps2x::iop::MemoryCardRequest&) override{return -1;}
    bool hasGuestFunction(uint32_t) const override{return false;}
    bool invokeGuestFunction(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t*) override{return false;}
    void log(ps2x::iop::LogLevel,std::string_view) override{}
};

void AudioInsideGuestCall() {
    AudioFixtureHost host;
    host.image.resize(0x1100);
    const uint8_t ident[]{0x7f,'E','L','F',1,1,1};
    std::memcpy(host.image.data(),ident,sizeof(ident));
    Put16(host.image,16,2);Put16(host.image,18,8);Put32(host.image,20,1);
    Put32(host.image,24,0x10000);Put32(host.image,28,52);
    Put16(host.image,40,52);Put16(host.image,42,32);Put16(host.image,44,1);
    Put16(host.image,46,40);
    Put32(host.image,52,1);Put32(host.image,56,0x100);
    Put32(host.image,60,0x10000);Put32(host.image,64,0x10000);
    Put32(host.image,68,0x1000);Put32(host.image,72,0x1000);
    Put32(host.image,76,7);Put32(host.image,80,0x100);
    const uint32_t code[]{
        0x3c081f90, // lui t0, SPU2 registers
        0x34098000, 0xa509019a, // enable core0
        0x34090001, 0xa50901b0, // core0 streaming transfer mode
        0x3c0a1f80, // lui t2, DMA register page
        0x3c0b0001, 0x356b0400, 0xad4b10c0, // MADR = authored data 0x10400
        0x3c0b0001, 0x356b0200, 0xad4b10c4, // BCR = 1 * 512 words
        0x3c0b0100, 0x356b0201, 0xad4b10c8, // start channel4 RAM -> SPU
        0x8d4b10c8, 0x3c0c0100, 0x016c5824, // load busy, independent load-delay slot, mask
        0x1560fffc, 0, // wait until complete, branch delay slot
        0x2402002a, 0x03e00008, 0 // return 42 after sound hardware consumes input
    };
    std::memcpy(host.image.data()+0x100,code,sizeof(code));
    std::fill(host.image.begin()+0x500,host.image.begin()+0xd00,uint8_t{0x24});
    ps2x::iop::detail::IopEmulator emulator(host);
    const auto loaded=emulator.loadModuleBuffer(0,nullptr,0);
    Require(loaded.moduleId>0 && loaded.startResult==42,
            "IOP guest startup could not complete DMA while inside callFunction");
    Require(emulator.cycles()>=49152 && emulator.cycles()<2000000,
            "IOP sound advanced without consuming its guest-clock budget");
    std::array<int16_t,512> pcm{};
    const auto expected=std::min<uint64_t>(256,(emulator.cycles()/(64u*768u))*64u);
    const auto read=emulator.readPcm(pcm.data(),256);
    Require(expected && read==expected && std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;}),
            "IOP callback did not expose its sound PCM through the subsystem boundary");
    emulator.reset();
    Require(emulator.readPcm(pcm.data(),256)==0,"IOP emulator reset retained PCM from an old module");
    std::cout<<"IOP guest-call DMA wait and PCM clock: PASS\n";
}

void IopModuleArguments() {
    AudioFixtureHost host;
    host.image=ModuleFixture();
    host.image.resize(0x500);
    Put16(host.image,16,2);Put32(host.image,24,0x10000);
    Put32(host.image,60,0x10000);Put32(host.image,64,0x10000);
    Put32(host.image,68,0x400);Put32(host.image,72,0x400);
    std::vector<uint32_t> code{0x00801021,0x8ca80000,0}; // v0=argc, t0=argv[0]
    std::vector<size_t> failBranches,returnBranches;
    auto failIf=[&](uint32_t instruction){failBranches.push_back(code.size());code.push_back(instruction);code.push_back(0);};
    failIf(0x11000000); // argv[0] must exist
    code.insert(code.end(),{0x91090000,0});
    failIf(0x11200000); // and contain a nonempty module path
    code.insert(code.end(),{0x00044080,0x00a84021,0x8d090000,0});
    failIf(0x15200000); // argv[argc] must be NULL
    auto doneIf=[&](uint32_t count){
        code.push_back(0x24080000|count);
        returnBranches.push_back(code.size());code.push_back(0x10880000);code.push_back(0);
    };
    doneIf(1);
    code.insert(code.end(),{0x8ca80004,0,0x91090000,0,0x00094a00,0x00491025});
    doneIf(2);
    code.insert(code.end(),{0x8ca80008,0,0x91090000,0,0x00094c00,0x00491025});
    const auto done=code.size();code.insert(code.end(),{0x03e00008,0});
    const auto fail=code.size();code.insert(code.end(),{0x2402ffff,0x03e00008,0});
    for(auto branch:failBranches)code[branch]|=uint16_t(fail-branch-1);
    for(auto branch:returnBranches)code[branch]|=uint16_t(done-branch-1);
    std::memcpy(host.image.data()+0x100,code.data(),code.size()*sizeof(uint32_t));
    ps2x::iop::detail::IopEmulator emulator(host);
    auto check=[&](const char* args,uint32_t bytes,uint32_t expected){
        emulator.reset();
        const auto result=emulator.loadModuleBuffer(0,args,bytes);
        Require(result.moduleId>0 && uint32_t(result.startResult)==expected,
                "IRX startup did not receive argc/argv with path and terminated argument pointers");
    };
    check(nullptr,0,1);
    check("core1",6,2u|('c'<<8));
    const char separated[]="core1\0stream";
    check(separated,sizeof(separated),3u|('c'<<8)|('s'<<16));
    check(separated,sizeof(separated)-1,3u|('c'<<8)|('s'<<16));
    emulator.reset();
    Require(emulator.loadModuleBuffer(0,nullptr,8).moduleId<0,
            "IRX accepted a nonempty null argument payload");
    std::cout<<"IOP module argc/argv: no args, separated args, final terminator, invalid payload: PASS\n";
}

AudioFixtureHost DelayedThreadImage() {
    AudioFixtureHost host;
    host.image.resize(0x1100);
    const uint8_t ident[]{0x7f,'E','L','F',1,1,1};
    std::memcpy(host.image.data(),ident,sizeof(ident));
    Put16(host.image,16,2);Put16(host.image,18,8);Put32(host.image,20,1);
    Put32(host.image,24,0x10000);Put32(host.image,28,52);
    Put16(host.image,40,52);Put16(host.image,42,32);Put16(host.image,44,1);
    Put16(host.image,46,40);Put32(host.image,52,1);Put32(host.image,56,0x100);
    Put32(host.image,60,0x10000);Put32(host.image,64,0x10000);
    Put32(host.image,68,0x1000);Put32(host.image,72,0x1000);
    Put32(host.image,76,7);Put32(host.image,80,0x100);
    const auto jal=[](uint32_t pc){return 0x0c000000u|(pc>>2u);};
    std::vector<uint32_t> entry{0x27bdffe0,0xafbf001c,0x00002821};
    for(unsigned i=0;i<8;++i) {
        const uint32_t create[]{0x3c040001,0x34840300u+i*32u,jal(0x10214),0,
                                0x00402021,jal(0x1021c),0};
        entry.insert(entry.end(),std::begin(create),std::end(create));
        const uint32_t descriptor[]{0,0,0x10100,0x400,10u+i};
        std::memcpy(host.image.data()+0x400+i*32,descriptor,sizeof(descriptor));
    }
    const uint32_t end[]{0x8fbf001c,0x00001021,0x27bd0020,0x03e00008,0};
    entry.insert(entry.end(),std::begin(end),std::end(end));
    Require(entry.size()*4<=0x100,"Authored IOP entry overlaps its workers");
    std::memcpy(host.image.data()+0x100,entry.data(),entry.size()*4);
    const uint32_t worker[]{
        0x27bdffe0,0xafbf001c,0x240403e8,jal(0x10224),0, // DelayThread(1000 us)
        0x3c080001,0x35080400,0x8d090000,0,0x25290001,0xad090000,
        0x8fbf001c,0,0x27bd0020,0x03e00008,0
    };
    std::memcpy(host.image.data()+0x200,worker,sizeof(worker));
    const uint32_t imports[]{0x41e00000,0,0x101,0x61626874,0x6573,
        0x03e00008,0x24000004,0x03e00008,0x24000006,0x03e00008,0x24000021,0,0};
    std::memcpy(host.image.data()+0x300,imports,sizeof(imports));
    return host;
}
void AudioDmaCriticalSection() {
    auto host=DelayedThreadImage();
    std::fill(host.image.begin()+0x100,host.image.end(),uint8_t{});
    const auto jal=[](uint32_t pc){return 0x0c000000u|(pc>>2u);};
    const uint32_t main[]{
        0x27bdffe0,0xafbf001c,
        0x24040024,0x24050001,0x3c060001,0x34c60180,0x00003821,jal(0x10214),0,
        0x24040024,jal(0x1021c),0, // EnableIntr(SPU DMA)
        0x3c040001,0x34840400,jal(0x10224),0, // CpuSuspendIntr(&saved)
        0x3c081f90,0x34098000,0xa509019a,0x34090001,0xa50901b0,
        0x3c0a1f80,0x3c0b0001,0x356b0500,0xad4b10c0,
        0x3c0b0001,0x356b0200,0xad4b10c4,
        0x3c0b0100,0x356b0201,0xad4b10c8,
        0x3c0b0001,0x256bffff,0x1560fffe,0, // spin through the audio DMA deadline
        0x3c080001,0x35080400,0x8d100004,0, // count while still in critical section
        0x8d040000,0,jal(0x1022c),0, // CpuResumeIntr(saved)
        0x02001021,0x8fbf001c,0,0x27bd0020,0x03e00008,0
    };
    Require(sizeof(main)<=0x180,"Authored interrupt fixture overlaps its callback");
    std::memcpy(host.image.data()+0x100,main,sizeof(main));
    const uint32_t handler[]{0x3c080001,0x35080404,0x8d090000,0,0x25290001,
                             0xad090000,0x24020001,0x03e00008,0};
    std::memcpy(host.image.data()+0x280,handler,sizeof(handler));
    const uint32_t imports[]{0x41e00000,0,0x101,0x72746e69,0x006e616d,
        0x03e00008,0x24000004,0x03e00008,0x24000006,
        0x03e00008,0x24000011,0x03e00008,0x24000012,0,0};
    std::memcpy(host.image.data()+0x300,imports,sizeof(imports));
    std::fill(host.image.begin()+0x600,host.image.begin()+0xe00,uint8_t{0x24});
    ps2x::iop::detail::IopEmulator emulator(host);
    const auto loaded=emulator.loadModuleBuffer(0,nullptr,0);
    Require(loaded.moduleId>0 && loaded.startResult==0,
            "SPU DMA interrupt ran while CpuSuspendIntr protected the guest");
    uint32_t delivered{};
    Require(emulator.readMemory(0x10404,&delivered,4) && delivered==1,
            "Masked SPU DMA completion was lost or duplicated after CpuResumeIntr");
    std::cout<<"IOP SPU DMA critical section: defer, resume, exactly one completion: PASS\n";
}

void IopDispatchBench() {
    for(const uint32_t quantum: {8u,128u}) {
        auto host=DelayedThreadImage();
        // Authored load/arithmetic/store loop in a real scheduled IRX thread.
        // Small EE checkpoints expose instruction fetch and scheduler overhead.
        const uint32_t worker[]{0x3c080001,0x35080400,0x8d090000,0,
            0x25290001,0xad090000,0x08004042,0};
        std::memcpy(host.image.data()+0x200,worker,sizeof(worker));
        ps2x::iop::detail::IopEmulator emulator(host);
        Require(emulator.loadModuleBuffer(0,nullptr,0).startResult==0,"Dispatch fixture did not start");
        constexpr uint64_t eeCycles=160'000'000;
        const auto before=emulator.instructions();
        uint64_t cpuBefore=0,cpuAfter=0;
#ifdef _WIN32
        Require(QueryThreadCycleTime(GetCurrentThread(),&cpuBefore),"CPU cycle counter unavailable");
#endif
        const auto start=std::chrono::steady_clock::now();
        for(uint64_t i=0;i<eeCycles;i+=quantum)emulator.runEeCycles(quantum);
        const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
#ifdef _WIN32
        Require(QueryThreadCycleTime(GetCurrentThread(),&cpuAfter),"CPU cycle counter unavailable");
#endif
        const auto instructions=emulator.instructions()-before;
        uint32_t count{};
        Require(emulator.readMemory(0x10400,&count,4) && count>100000 && instructions==eeCycles/8,
                "Dispatch optimization changed executed guest work");
        std::cout<<"dispatch_bench quantum="<<quantum<<" instructions="<<instructions
                 <<" checksum="<<count<<" ns_per_instruction="<<ns/instructions
                 <<" cpu_cycles_per_instruction="<<double(cpuAfter-cpuBefore)/instructions<<'\n';
    }
}
void IopIdleContract(bool bench=false) {
    auto host=DelayedThreadImage();
    ps2x::iop::detail::IopEmulator emulator(host);
    // Populate an empty idle interval, then load code creating ready threads.
    emulator.runEeCycles(800);
    Require(emulator.loadModuleBuffer(0,nullptr,0).startResult==0,"Authored delayed-thread IRX failed");
    for(unsigned i=0;i<20000;++i)emulator.runEeCycles(8);
    uint32_t count{};
    Require(emulator.readMemory(0x10400,&count,4) && count==0,"Delayed IOP threads woke early");
    for(unsigned i=0;i<20000;++i)emulator.runEeCycles(8);
    Require(emulator.readMemory(0x10400,&count,4) && count==8,"Idle IOP skipped a delayed thread wake");
    const auto before=emulator.cycles();
    const auto start=std::chrono::steady_clock::now();
    constexpr unsigned calls=400000;
    if(bench)for(unsigned i=0;i<calls;++i)emulator.runEeCycles(8);
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(bench) {
        Require(emulator.cycles()-before==calls,"Idle fast-forward changed IOP clock accounting");
        std::cout<<"calls,guest_cycles,elapsed_ms\n"<<calls<<','<<emulator.cycles()-before<<','<<elapsed<<'\n';
    }
    else std::cout<<"IOP idle interval, new module, delayed thread wake: PASS\n";
}
}
