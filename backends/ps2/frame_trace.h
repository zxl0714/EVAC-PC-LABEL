#pragma once
#include "input_trace.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace cave::ps2::frame_trace {
struct Field {
    int64_t deadline=0,vblank=0,enqueued=0,presentStart=0,flushed=0,acquired=0,
        scanned=0,submitted=0,presented=0,gpuWait=0,waitRequested=0,waitElapsed=0,waitLate=0;
};
// One EE writer and one presentation writer own separate fields of each row.
// Configuration precedes their creation; serialization follows their join.
inline std::unique_ptr<std::vector<Field>> rows;
// EE-owner-only slow scopes. Keep them in memory; never write a log while
// generating sound. Enabled only with the explicit frame timing capture.
struct SlowEvent {int64_t start{},elapsed{};const char* kind{};uint64_t detail{};};
inline std::array<SlowEvent,4096> slowEvents{};
inline uint64_t slowWritten{};
inline int64_t Stamp(std::chrono::steady_clock::time_point value=std::chrono::steady_clock::now()) {
    return std::chrono::duration_cast<std::chrono::microseconds>(value.time_since_epoch()).count();
}
class SlowScope {
    const char* kind;uint64_t detail;int64_t start;
public:
    SlowScope(const char* name,uint64_t value=0):kind(name),detail(value),start(rows?Stamp():0){}
    ~SlowScope(){if(start){const auto elapsed=Stamp()-start;if(elapsed>=2000)
        slowEvents[slowWritten++%slowEvents.size()]={start,elapsed,kind,detail};}}
};
inline Field* Get(uint64_t tick) {
    return rows && tick && tick<=rows->size()?&(*rows)[size_t(tick-1)]:nullptr;
}
inline void Configure(bool enabled){rows=enabled?std::make_unique<std::vector<Field>>(36000):nullptr;slowWritten=0;}
inline thread_local int64_t waitRequested=0,waitElapsed=0,waitLate=0;
inline void WaitReturned(std::chrono::steady_clock::time_point started,std::chrono::steady_clock::time_point deadline) {
    if(!rows)return;
    const auto elapsed=Stamp()-Stamp(started);
    if(elapsed>waitElapsed){waitRequested=Stamp(deadline)-Stamp(started);waitElapsed=elapsed;waitLate=Stamp()-Stamp(deadline);}
}
inline void Vblank(uint64_t tick,std::chrono::steady_clock::time_point deadline) {
    input_trace::Tick(tick);
    if(auto* row=Get(tick)){row->deadline=Stamp(deadline);row->vblank=Stamp();row->waitRequested=waitRequested;row->waitElapsed=waitElapsed;row->waitLate=waitLate;waitElapsed=0;}
}
inline void Save(const std::filesystem::path& path) {
    if(!rows)return;
    std::ofstream out(path);
    if(!out)throw std::runtime_error("Cannot write frame timing output");
    out<<"frame,present_return_us,interval_us,vblank_late_us,presentation_us,previous_gpu_wait_us,flush_us,acquire_us,scanout_us,submit_us,present_us,queue_us,present_qpc_us,wait_requested_us,wait_elapsed_us,wait_late_us\n";
    int64_t first=0,previous=0;uint64_t tick=0;
    for(const auto& row:*rows){
        ++tick;if(!row.presented)continue;
        if(!first)first=row.presented;
        out<<tick<<','<<row.presented-first<<','<<(previous?row.presented-previous:0)<<','
           <<row.vblank-row.deadline<<','<<row.presented-row.presentStart<<','<<row.gpuWait<<','
           <<row.flushed-row.presentStart<<','<<row.acquired-row.flushed<<','
           <<row.scanned-row.acquired<<','<<row.submitted-row.scanned<<','
           <<row.presented-row.submitted<<','<<row.presentStart-row.enqueued<<','<<row.presented<<','<<row.waitRequested<<','<<row.waitElapsed<<','<<row.waitLate<<'\n';
        previous=row.presented;
    }
    if(!out)throw std::runtime_error("Frame timing output failed");
    std::ofstream slow(path.string()+".slow.csv");
    slow<<"qpc_us,elapsed_us,kind,detail\n";
    const auto begin=slowWritten>slowEvents.size()?slowWritten-slowEvents.size():0;
    for(auto i=begin;i<slowWritten;++i){const auto& e=slowEvents[i%slowEvents.size()];
        slow<<e.start<<','<<e.elapsed<<','<<e.kind<<','<<e.detail<<'\n';}
    if(!slow)throw std::runtime_error("Slow frame scope output failed");
}
}
