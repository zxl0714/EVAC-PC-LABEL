#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace cave::ps2 {
// Explicit diagnostics only. Each ring has a single owner; audio shutdown
// drains the consumer before IopMemory destroys this capture.
class PcmQueueTrace {
    struct Event {int64_t us{};uint32_t requested{},completed{},late{};};
    struct Ring {std::vector<Event> events;uint64_t written{};};
    static constexpr size_t Capacity=65536;
    std::filesystem::path path;
    std::array<Ring,2> rings;
public:
    PcmQueueTrace() {
        if(const auto* value=std::getenv("EVAC_PS2_AUDIO_QUEUE_TRACE");value && *value) {
            path=value;
            if(!path.is_absolute() || std::filesystem::exists(path))
                throw std::runtime_error("Audio queue trace needs a fresh absolute file path");
            for(auto& ring:rings)ring.events.resize(Capacity);
        }
    }
    void Record(unsigned direction,size_t requested,size_t completed,size_t late=0) noexcept {
        if(path.empty())return;
        auto& ring=rings[direction];
        const auto stamp=std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        ring.events[ring.written++%Capacity]={stamp,uint32_t(requested),uint32_t(completed),uint32_t(late)};
    }
    ~PcmQueueTrace() {
        if(path.empty())return;
        std::ofstream out(path);
        out<<"direction,qpc_us,requested_frames,completed_frames,late_frames\n";
        for(unsigned direction=0;direction<rings.size();++direction) {
            const auto& ring=rings[direction];
            const auto count=std::min<uint64_t>(ring.written,Capacity);
            for(uint64_t i=ring.written-count;i<ring.written;++i) {
                const auto& e=ring.events[i%Capacity];
                out<<direction<<','<<e.us<<','<<e.requested<<','<<e.completed<<','<<e.late<<'\n';
            }
        }
    }
};
}
