#include "gs_device.h"
#include "gs_device_sink.h"
#include "gs_trace.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
double CpuMilliseconds() {
    FILETIME created{},ended{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user))throw std::runtime_error("Cannot read process CPU time");
    const auto value=[](const FILETIME& time){return (uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;};
    return double(value(kernel)+value(user))/10000.0;
}
std::string Quoted(const std::string& source) {
    std::string result="\"";
    for(unsigned char c:source){if(c=='"' || c=='\\')result+='\\';if(c<32)result+='?';else result+=char(c);}
    return result+'"';
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc<2)throw std::invalid_argument("Usage: ps2_gs_replay trace.evacgs [--seconds 1] [--warmup 1] [--iterations 1] [--trace-limit-mib 1024]");
        double seconds=1;unsigned warmup=1,minimum=1;
        uint64_t traceMib=1024;
        for(int i=2;i<argc;i+=2) {
            if(i+1>=argc)throw std::invalid_argument("Missing replay option value");
            const std::wstring option(argv[i]);
            if(option==L"--seconds")seconds=std::stod(argv[i+1]);
            else if(option==L"--warmup")warmup=std::stoul(argv[i+1]);
            else if(option==L"--iterations")minimum=std::stoul(argv[i+1]);
            else if(option==L"--trace-limit-mib")traceMib=std::stoull(argv[i+1]);
            else throw std::invalid_argument("Unknown replay option");
        }
        if(!(seconds>=0 && seconds<=30) || warmup>10 || !minimum || minimum>10000)throw std::invalid_argument("Replay options out of range");
        if(!traceMib || traceMib>4096)throw std::invalid_argument("Trace limit must be 1..4096 MiB");
        cave::ps2::GsTraceWorkload workload{std::filesystem::path(argv[1]),traceMib*1024ull*1024u};
        const auto source=workload.Stats();if(!source.frames)throw std::runtime_error("Trace contains no complete fields");
        // A microbench warms only the pipelines used by its workload. PGS's
        // normal startup concurrently precompiles every potential variant;
        // counting those background tasks would measure startup, not replay.
        // This environment setting is confined to this benchmark process.
        if(_putenv_s("PGS_SKIP_COMPILATION_TASKS","1"))throw std::runtime_error("Cannot select workload-only shader warmup");
        cave::ps2::GsDevice device;device.SetOfflineReplay(true);
        cave::ps2::GsDeviceSink sink(device,[&](const cave::ps2::GsFrame& field){device.RenderField(field);},[&]{device.WaitField();});
        for(unsigned i=0;i<warmup;++i){device.Reset();workload.Replay(sink);device.WaitField();}
        const auto start=std::chrono::steady_clock::now();const double cpuStart=CpuMilliseconds();
        uint64_t iterations=0;
        do {
            device.Reset();workload.Replay(sink);device.WaitField();++iterations;
        }while(iterations<minimum || std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<seconds);
        const double cpu=CpuMilliseconds()-cpuStart;
        const double wall=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        const auto fields=iterations*source.frames;
        std::cout<<std::fixed<<std::setprecision(6)
            <<"{\"gpu\":"<<Quoted(device.GpuName())<<",\"iterations\":"<<iterations<<",\"fields\":"<<fields
            <<",\"commands_per_iteration\":"<<source.commands<<",\"wall_ms\":"<<wall<<",\"cpu_ms\":"<<cpu
            <<",\"wall_ms_per_field\":"<<wall/double(fields)<<",\"cpu_ms_per_field\":"<<cpu/double(fields)
            <<",\"cpu_ms_per_second\":"<<cpu*1000.0/wall<<",\"cpu_cores\":"<<cpu/wall
            <<",\"shader_warmup_iterations\":"<<warmup<<",\"background_precompile\":false"
            <<",\"vram_snapshots\":"<<device.ReadbackRequests()<<"}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
