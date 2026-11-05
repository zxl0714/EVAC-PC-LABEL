#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "gs_display.h"
#include "runtime/gs/gs_types.h"
#include "../../common/windows_timer_resolution.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace {
uint64_t ThreadCycles() {
    ULONG64 value{};
    if(!QueryThreadCycleTime(GetCurrentThread(),&value))throw std::runtime_error("Cannot read thread cycles");
    return value;
}
double ThreadMilliseconds() {
    FILETIME created{},ended{},kernel{},user{};
    if(!GetThreadTimes(GetCurrentThread(),&created,&ended,&kernel,&user))throw std::runtime_error("Cannot read thread CPU time");
    const auto ticks=[](const FILETIME& t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    return double(ticks(kernel)+ticks(user))/10000.;
}
}
int main(int argc,char** argv) {
    try {
        using Clock=std::chrono::steady_clock;
        const unsigned fields=argc>1?std::stoul(argv[1]):600;
        const bool fullscreen=argc>2 && std::string_view(argv[2])=="fullscreen";
        if(fields<60 || fields>3600)throw std::invalid_argument("Use 60..3600 measured fields");
        // Workload-only warmup; no all-variant shader compiler background load.
        if(_putenv_s("PGS_SKIP_COMPILATION_TASKS","1"))throw std::runtime_error("Cannot select workload warmup");
        // Match the actual game's physical-pixel output at non-100% desktop DPI.
        using SetDpiContext=BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        const auto dpi=reinterpret_cast<SetDpiContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext"));
        if(!dpi || !dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))SetProcessDPIAware();
        cave::ScopedTimerResolution precision;precision.Enable();
        cave::ps2::GsDisplay display(1280,720,L"EVAC PC LABEL | private WSI comparison",false);
        if(fullscreen)display.ToggleFullscreen();
        auto& gs=display.Device();
        std::vector<uint8_t> empty(cave::ps2::GsDevice::VramBytes);gs.WriteVram(0,empty);
        gs.WriteRegister(GS_REG_PRMODECONT,1);gs.WriteRegister(GS_REG_XYOFFSET_1,0);
        gs.WriteRegister(GS_REG_FRAME_1,uint64_t(10)<<16);
        gs.WriteRegister(GS_REG_ZBUF_1,uint64_t(1)<<32);gs.WriteRegister(GS_REG_TEST_1,0x30000);
        gs.WriteRegister(GS_REG_SCISSOR_1,(uint64_t(639)<<16)|(uint64_t(223)<<48));
        gs.WriteRegister(GS_REG_COLCLAMP,1);gs.WriteRegister(GS_REG_PRIM,6|0x100);
        cave::ps2::GsFrame request{};
        request.smode1=(uint64_t(32)<<3)|(uint64_t(2)<<13);
        request.pmode=1|(1u<<5)|(255u<<8);request.dispfb1=uint64_t(10)<<9;
        request.display1=636|(uint64_t(25)<<12)|(uint64_t(3)<<23)|
                         (uint64_t(2559)<<32)|(uint64_t(223)<<44);
        const std::vector<uint8_t> texture(16u*16u*4u,0x7f);
        const std::array<uint64_t,4> upload{(uint64_t(0x100)<<32)|(uint64_t(1)<<48),
                                          0,16|(uint64_t(16)<<32),0};
        std::vector<double> intervals;intervals.reserve(fields);
        Clock::time_point measuredStart{},previous{};
        uint64_t cyclesBefore{};double cpuBefore{};
        constexpr unsigned warmup=120;
        const auto start=Clock::now();
        for(unsigned field=0;field<fields+warmup;++field) {
            if(field==warmup){measuredStart=Clock::now();cyclesBefore=ThreadCycles();cpuBefore=ThreadMilliseconds();}
            gs.UploadImage(upload,texture);
            gs.WriteRegister(GS_REG_RGBAQ,0x3f800000ff604020ull);
            gs.WriteRegister(GS_REG_XYZ2,0);
            gs.WriteRegister(GS_REG_XYZ2,uint64_t(640*16)|(uint64_t(224*16)<<16));
            request.vsyncTick=field;
            if(!display.Present(request))throw std::runtime_error("Probe window closed before completion");
            const auto now=Clock::now();
            if(field>=warmup && field>warmup)intervals.push_back(std::chrono::duration<double,std::milli>(now-previous).count());
            previous=now;
            // Authored 60 Hz producer only; no original game or EE scheduling.
            std::this_thread::sleep_until(start+std::chrono::nanoseconds(uint64_t(field+1)*1000000000ull/60u));
        }
        const auto cycles=ThreadCycles()-cyclesBefore;
        const auto cpu=ThreadMilliseconds()-cpuBefore;
        const auto wall=std::chrono::duration<double,std::milli>(Clock::now()-measuredStart).count();
        display.WaitPreviousFrame();
        if(gs.ReadbackRequests())throw std::runtime_error("WSI comparison read back VRAM");
        std::sort(intervals.begin(),intervals.end());
        std::cout<<std::fixed<<std::setprecision(6)
          <<"{\"fields\":"<<fields<<",\"warmup\":"<<warmup<<",\"wall_ms\":"<<wall
          <<",\"owner_cpu_ms\":"<<cpu<<",\"owner_cores\":"<<cpu/wall
          <<",\"owner_cycles_per_field\":"<<double(cycles)/fields
          <<",\"median_interval_ms\":"<<intervals[intervals.size()/2]
          <<",\"max_interval_ms\":"<<intervals.back()
          <<",\"readbacks\":"<<gs.ReadbackRequests()<<",\"fullscreen\":"<<(fullscreen?"true":"false")<<"}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
