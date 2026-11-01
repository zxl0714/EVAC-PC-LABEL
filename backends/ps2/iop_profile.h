#pragma once
#include "audio_trace.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <syncstream>

namespace cave::ps2 {
// Diagnostic sampling on the IOP owner thread. Fixed storage and one sample
// per 1024 guest instructions; ordinary gameplay never records samples.
class IopProfile {
    struct Entry { uint32_t pc{}, count{}; };
    std::array<Entry,4096> entries_{};
    uint32_t samples_{};
public:
    void Reset() { entries_={}; samples_=0; }
    void Record(uint32_t pc) {
        auto index=(pc>>2u)%entries_.size();
        for(size_t probe=0;probe<entries_.size();++probe) {
            auto& entry=entries_[index];
            if(!entry.count || entry.pc==pc){entry.pc=pc;++entry.count;break;}
            index=(index+1u)%entries_.size();
        }
        if(++samples_!=16384)return;
        auto sorted=entries_;
        std::partial_sort(sorted.begin(),sorted.begin()+12,sorted.end(),
            [](const auto& a,const auto& b){return a.count>b.count;});
        std::osyncstream line(std::cerr);
        line<<"[ps2-iop-profile] samples="<<samples_;
        for(size_t i=0;i<12 && sorted[i].count;++i)
            line<<" pc=0x"<<std::hex<<sorted[i].pc<<std::dec<<":"<<sorted[i].count;
        line<<'\n';
        Reset();
    }
};
}
