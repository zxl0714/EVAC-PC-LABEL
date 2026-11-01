#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <syncstream>

namespace cave::ps2::audio_trace {
inline bool enabled = false;
inline std::chrono::steady_clock::time_point started{};
inline void Configure(bool value) {
    enabled=value;
    started=std::chrono::steady_clock::now();
}
inline int64_t Milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-started).count();
}
inline void Rpc(uint32_t sid,uint32_t function,uint32_t send,uint32_t receive) {
    if(!enabled)return;
    struct Entry {uint32_t sid{},function{},count{};};
    static std::mutex mutex;
    static std::array<Entry,64> entries{};
    std::lock_guard lock(mutex);
    for(auto& entry:entries) {
        if(entry.count && (entry.sid!=sid || entry.function!=function))continue;
        if(entry.count>=4)return;
        entry.sid=sid;entry.function=function;++entry.count;
        std::osyncstream(std::cerr)<<"[ps2-audio-rpc] ms="<<Milliseconds()
            <<" sid=0x"<<std::hex<<sid<<" function=0x"<<function<<std::dec
            <<" send="<<send<<" receive="<<receive<<'\n';
        return;
    }
}
inline void Register(uint32_t address,uint32_t value) {
    if(!enabled)return;
    const auto offset=address&0x3fcu;
    if(offset!=0x1a0u && offset!=0x1a4u && offset!=0x198u && offset!=0x1b0u)return;
    if(!value && (offset==0x1a0u || offset==0x1a4u))return;
    static std::atomic<uint32_t> budget{};
    if(budget.fetch_add(1,std::memory_order_relaxed)>=192)return;
    std::osyncstream(std::cerr)<<"[ps2-audio-register] ms="<<Milliseconds()
        <<" address=0x"<<std::hex<<address<<" value=0x"<<value<<std::dec<<'\n';
}
inline void Dma(unsigned core,uint32_t source,uint64_t words,uint32_t chcr,uint32_t tsa) {
    if(!enabled)return;
    // This path has one IOP owner. Avoid spending the whole trace budget on
    // the initialization driver's rapid ping-pong DMA loop.
    static std::array<uint32_t,2> counts{};
    static std::array<uint32_t,2> bankCounts{};
    static std::array<int64_t,2> lastMs{};
    if(core>1 || counts[core]>=256)return;
    const auto now=Milliseconds();
    const bool bank=tsa>=0x5000u && bankCounts[core]<64u;
    if(!bank && counts[core]>=4 && now-lastMs[core]<1000)return;
    if(bank)++bankCounts[core];
    ++counts[core];lastMs[core]=now;
    std::osyncstream(std::cerr)<<"[ps2-audio-dma] ms="<<Milliseconds()<<" core="<<core
        <<" source=0x"<<std::hex<<source<<" chcr=0x"<<chcr<<" tsa=0x"<<tsa
        <<std::dec<<" words="<<words<<'\n';
}
}
