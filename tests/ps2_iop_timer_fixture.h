#pragma once

// Authored IRX: start a periodic timer after an empty idle interval. Its callback
// records delivery and stops when a host-visible flag is set. No game code/data.
namespace {
void IopScheduledTimerContract() {
    auto host=DelayedThreadImage();
    std::fill(host.image.begin()+0x100,host.image.end(),uint8_t{});
    const auto jal=[](uint32_t pc){return 0x0c000000u|(pc>>2u);};
    const uint32_t entry[]{
        0x27bdffe0,0xafbf001c,0xafb00018,
        0x24040001,0x24050010,0x24060001,jal(0x10214),0, // AllocHardTimer
        0x00408021,0x3c080001,0xad100408, // publish the allocated timer ID
        0x00402021,0x24050001,0x00003021,0x24070001,jal(0x1021c),0,
        0x02002021,0x24050040,0x3c060001,0x34c60180,
        0x3c070001,0x34e70400,jal(0x10224),0, // SetTimerHandler(64, callback, common)
        0x02002021,jal(0x1022c),0, // StartHardTimer
        0x8fbf001c,0x8fb00018,0x00001021,0x27bd0020,0x03e00008,0
    };
    Require(sizeof(entry)<=0x180,"Timer entry overlaps its callback");
    std::memcpy(host.image.data()+0x100,entry,sizeof(entry));
    const uint32_t callback[]{
        0x8c880000,0,0x25080001,0xac880000, // ++*common
        0x8c880004,0,0x11000003,0x24020040, // continue every 64 cycles
        0x00001021,0, // flag set -> return 0, stop
        0x03e00008,0
    };
    std::memcpy(host.image.data()+0x280,callback,sizeof(callback));
    const uint32_t imports[]{0x41e00000,0,0x101,0x726d6974,0x006e616d,
        0x03e00008,0x24000004,0x03e00008,0x24000016,
        0x03e00008,0x24000014,0x03e00008,0x24000017,0,0};
    std::memcpy(host.image.data()+0x300,imports,sizeof(imports));
    const auto initialImage=host.image;
    for(const uint32_t quantum:{8u,128u}) {
        host.image=initialImage;
        ps2x::iop::detail::IopEmulator emulator(host);
        emulator.runEeCycles(800); // populate the no-timer idle cache first
        Require(emulator.loadModuleBuffer(0,nullptr,0).startResult==0,"Authored timer IRX failed");
        auto count=[&]{uint32_t v{};Require(emulator.readMemory(0x10400,&v,4),"Timer count read failed");return v;};
        Require(count()==0,"New timer callback ran before its deadline");
        for(unsigned i=0;i<4096/quantum;++i)emulator.runEeCycles(quantum);
        const auto periodic=count();
        Require(periodic>=5 && periodic<=10,"Active periodic timer was skipped or over-delivered");
        const uint32_t stop=1;
        Require(emulator.writeMemory(0x10404,&stop,4),"Timer stop flag write failed");
        for(unsigned i=0;i<4096/quantum;++i)emulator.runEeCycles(quantum);
        Require(count()==periodic+1,"Timer callback stop result was ignored");
        const auto stopped=count();
        for(unsigned i=0;i<4096/quantum;++i)emulator.runEeCycles(quantum);
        Require(count()==stopped,"Stopped timer delivered another callback");
        // A distinct authored module changes an existing timer after the idle
        // deadline has been cached as infinity. Its code/data do not overwrite
        // the first module's callback or common state.
        uint32_t actionBase=0x30000;
        auto action=[&](bool restart) {
            const uint32_t base=actionBase;actionBase+=0x1000;
            auto image=DelayedThreadImage().image;
            std::fill(image.begin()+0x100,image.end(),uint8_t{});
            Put32(image,24,base);Put32(image,60,base);Put32(image,64,base);
            std::vector<uint32_t> code{0x27bdffe0,0xafbf001c,
                0x3c080001,0x8d040408,0,jal(base+0x214),0}; // stop (already stopped is OK)
            if(restart) {
                const uint32_t start[]{0x3c080001,0x8d040408,0,
                    0x24050020,0x3c060001,0x34c60180,0x3c070001,0x34e70400,
                    jal(base+0x21c),0,0x3c080001,0x8d040408,0,jal(base+0x224),0};
                code.insert(code.end(),std::begin(start),std::end(start));
            }
            const uint32_t end[]{0x8fbf001c,0x00001021,0x27bd0020,0x03e00008,0};
            code.insert(code.end(),std::begin(end),std::end(end));
            std::memcpy(image.data()+0x100,code.data(),code.size()*4);
            const uint32_t api[]{0x41e00000,0,0x101,0x726d6974,0x006e616d,
                0x03e00008,0x24000018,0x03e00008,0x24000014,
                0x03e00008,0x24000017,0,0};
            std::memcpy(image.data()+0x300,api,sizeof(api));
            host.image=std::move(image);
            Require(emulator.loadModuleBuffer(0,nullptr,0).startResult==0,"Timer mutation IRX failed");
        };
        const uint32_t resume=0;
        Require(emulator.writeMemory(0x10404,&resume,4),"Timer resume flag write failed");
        action(true);
        for(unsigned i=0;i<4096/quantum;++i)emulator.runEeCycles(quantum);
        const auto restarted=count();
        Require(restarted>stopped,"Timer restart did not invalidate the idle deadline");
        action(false);
        const auto explicitlyStopped=count();
        for(unsigned i=0;i<4096/quantum;++i)emulator.runEeCycles(quantum);
        Require(count()==explicitlyStopped,"StopHardTimer left a cached callback pending");
        const auto cycles=emulator.cycles(),instructions=emulator.instructions();
        emulator.reset();emulator.runEeCycles(4096);
        Require(count()==0,"Reset retained timer callback state");
        std::cout<<"IOP timer quantum="<<quantum<<" periodic="<<periodic<<" stopped="<<stopped
                 <<" restarted="<<restarted
                 <<" cycles="<<cycles<<" instructions="<<instructions<<": PASS\n";
    }
}
}
