#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace cave::ps2::input_trace {
enum class Kind : uint8_t { HostKey,PadRead,VBlank,DisplayWrite,FrameQueue,PresentStart,
                            Submit,PresentEnd,Feedback,ClockLink,GsCommand,ReadyQueue };
struct Event { int64_t qpcUs{};uint64_t frame{},a{},b{},c{};Kind kind{}; };
struct Storage {
    std::filesystem::path path;
    std::unique_ptr<Event[]> events;
    const size_t capacity;
    std::atomic<size_t> count{};
    std::atomic<uint64_t> frame{};
    bool saved=false;
    Storage(const std::filesystem::path& file,size_t limit):path(file),events(new Event[limit]),capacity(limit){}
};
// Configuration precedes worker creation. Serialization/destruction follows
// their join. Each writer reserves a distinct row; no frame-time file IO.
inline std::unique_ptr<Storage> storage;
inline bool Enabled() noexcept{return bool(storage);}
inline uint64_t CurrentField() noexcept{return storage?storage->frame.load(std::memory_order_relaxed):0;}
inline int64_t Stamp() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void Record(Kind kind,uint64_t frame=0,uint64_t a=0,uint64_t b=0,uint64_t c=0) noexcept {
    if(!storage)return;
    const auto slot=storage->count.fetch_add(1,std::memory_order_relaxed);
    if(slot<storage->capacity)storage->events[slot]={Stamp(),frame?frame:storage->frame.load(std::memory_order_relaxed),a,b,c,kind};
}
inline void Tick(uint64_t frame) noexcept {
    if(storage){storage->frame.store(frame,std::memory_order_relaxed);Record(Kind::VBlank,frame);}
}
inline const char* Name(Kind kind) noexcept {
    switch(kind) {
    case Kind::HostKey:return "host_key";case Kind::PadRead:return "pad_read";
    case Kind::VBlank:return "vblank";case Kind::DisplayWrite:return "display_write";
    case Kind::FrameQueue:return "frame_queue";case Kind::PresentStart:return "present_start";
    case Kind::Submit:return "submit";case Kind::PresentEnd:return "present_end";
    case Kind::Feedback:return "feedback_unknown_stage";case Kind::ClockLink:return "clock_link";
    case Kind::GsCommand:return "gs_command";case Kind::ReadyQueue:return "ready_queue";
    }
    return "unknown";
}
inline void Save() {
    if(!storage || storage->saved)return;
    if(std::filesystem::exists(storage->path))throw std::runtime_error("Input trace output already exists");
    std::ofstream out(storage->path);
    if(!out)throw std::runtime_error("Cannot open input trace output");
    const auto count=storage->count.load(std::memory_order_relaxed),limit=std::min(count,storage->capacity);
    out<<"# format=1 recorded="<<limit<<" overflow="<<count-limit<<'\n';
    out<<"sequence,qpc_us,kind,frame,a,b,c\n";
    for(size_t i=0;i<limit;++i){const auto& row=storage->events[i];out<<i<<','<<row.qpcUs<<','<<Name(row.kind)<<','
        <<row.frame<<','<<row.a<<','<<row.b<<','<<row.c<<'\n';}
    out.flush();if(!out)throw std::runtime_error("Cannot write input trace output");
    storage->saved=true;
}
class Scope {
public:
    explicit Scope(const std::filesystem::path& path,size_t capacity=200000) {
        if(storage)throw std::logic_error("Input trace already configured");
        if(path.empty())return;
        if(!capacity || capacity>1000000 || std::filesystem::exists(path))
            throw std::invalid_argument("Invalid input trace destination or capacity");
        storage=std::make_unique<Storage>(path,capacity);
    }
    ~Scope() {
        try{Save();}catch(const std::exception& error){std::cerr<<"[ps2-input-trace] "<<error.what()<<'\n';}
        storage.reset();
    }
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
}
