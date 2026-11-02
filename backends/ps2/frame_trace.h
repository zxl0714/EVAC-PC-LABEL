#pragma once
#include "input_trace.h"
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
        scanned=0,submitted=0,presented=0,gpuWait=0;
};
// One EE writer and one presentation writer own separate fields of each row.
// Configuration precedes their creation; serialization follows their join.
inline std::unique_ptr<std::vector<Field>> rows;
inline int64_t Stamp(std::chrono::steady_clock::time_point value=std::chrono::steady_clock::now()) {
    return std::chrono::duration_cast<std::chrono::microseconds>(value.time_since_epoch()).count();
}
inline Field* Get(uint64_t tick) {
    return rows && tick && tick<=rows->size()?&(*rows)[size_t(tick-1)]:nullptr;
}
inline void Configure(bool enabled){rows=enabled?std::make_unique<std::vector<Field>>(36000):nullptr;}
inline void Vblank(uint64_t tick,std::chrono::steady_clock::time_point deadline) {
    input_trace::Tick(tick);
    if(auto* row=Get(tick)){row->deadline=Stamp(deadline);row->vblank=Stamp();}
}
inline void Save(const std::filesystem::path& path) {
    if(!rows)return;
    std::ofstream out(path);
    if(!out)throw std::runtime_error("Cannot write frame timing output");
    out<<"frame,present_return_us,interval_us,vblank_late_us,presentation_us,previous_gpu_wait_us,flush_us,acquire_us,scanout_us,submit_us,present_us,queue_us,present_qpc_us\n";
    int64_t first=0,previous=0;uint64_t tick=0;
    for(const auto& row:*rows){
        ++tick;if(!row.presented)continue;
        if(!first)first=row.presented;
        out<<tick<<','<<row.presented-first<<','<<(previous?row.presented-previous:0)<<','
           <<row.vblank-row.deadline<<','<<row.presented-row.presentStart<<','<<row.gpuWait<<','
           <<row.flushed-row.presentStart<<','<<row.acquired-row.flushed<<','
           <<row.scanned-row.acquired<<','<<row.submitted-row.scanned<<','
           <<row.presented-row.submitted<<','<<row.presentStart-row.enqueued<<','<<row.presented<<'\n';
        previous=row.presented;
    }
    if(!out)throw std::runtime_error("Frame timing output failed");
}
}
