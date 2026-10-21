#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace sdoj::latency {
// The normal core's slowdown calculator consumes the original draw pipeline's
// metrics, not host execution time. Keep identities across the two reused
// command buffers; a held/no-parse wrapper does not replace a parsed sample.
class RenderMetrics {
public:
    using Pair=std::array<std::uint32_t,2>;
    struct Selection { Pair values; std::uint64_t source; bool held; };
    void Reset() { entries_={};reading_=consumed_=0;frame_.reset();selected_.reset(); }
    void Complete(std::uint64_t logic,std::uint32_t buffer,std::uint32_t baseline,std::uint32_t depth) {
        entries_[logic%entries_.size()]={logic,buffer,std::uint32_t(baseline+depth-1),{},false};
    }
    void BeginRead(std::uint32_t buffer) {
        reading_=0;
        for(const auto& entry:entries_)
            if(entry.buffer==buffer && entry.logic>reading_)reading_=entry.logic;
    }
    bool Capture(Pair values) {
        const auto logic=reading_;reading_=0;
        if(!logic)return true; // Native initialization or a no-parse wrapper.
        auto* entry=Find(logic);
        if(!entry || (entry->sampled && entry->values!=values))return false;
        entry->values=values;entry->sampled=true;return true;
    }
    // Model the ORIGINAL renderer once per video frame, before its late wake.
    // A completed result is published at baseline+depth-1, AFTER that frame's
    // draw, so only an earlier native counter is visible here. This tracks
    // resource waits and pauses without equating video ticks to logic frames.
    void Tick(std::uint32_t frame,std::uint32_t calls) {
        if(frame_ && *frame_==frame)return;
        frame_=frame;
        const Entry* source=nullptr;
        for(const auto& entry:entries_) {
            const auto age=std::uint32_t(calls-entry.due);
            if(entry.logic && age && age<0x80000000u &&
               (!source || entry.logic>source->logic))source=&entry;
        }
        if(!source || source->logic==consumed_) {
            selected_=Selection{{0,0},0,true};return;
        }
        // A skipped/missing real parse is not evidence of zero work.
        if(!source->sampled) { selected_.reset();return; }
        consumed_=source->logic;
        selected_=Selection{source->values,source->logic,false};
    }
    std::optional<Selection> OriginalInputs() const { return selected_; }
private:
    struct Entry {
        std::uint64_t logic{};
        std::uint32_t buffer{},due{};
        Pair values{};
        bool sampled{};
    };
    Entry* Find(std::uint64_t logic) {
        auto& entry=entries_[logic%entries_.size()];
        return entry.logic==logic?&entry:nullptr;
    }
    const Entry* Find(std::uint64_t logic) const {
        const auto& entry=entries_[logic%entries_.size()];
        return entry.logic==logic?&entry:nullptr;
    }
    std::array<Entry,8> entries_{};
    std::uint64_t reading_{},consumed_{};
    std::optional<std::uint32_t> frame_;
    std::optional<Selection> selected_;
};
}
