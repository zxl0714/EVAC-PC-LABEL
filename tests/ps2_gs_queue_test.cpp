#include "gs_command_queue.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace std::chrono_literals;
using cave::ps2::GsCommandQueue;
void Require(bool good,const char* message){if(!good)throw std::runtime_error(message);}
class Consumer final : public cave::ps2::GsCommandBackend {
public:
    std::vector<unsigned> order;
    std::vector<uint8_t> image;
    std::array<uint64_t,4> setup{};
    bool failFifo=false;
    std::function<void()> beforeRead;
    uint64_t frame=0;
    void Initialize(std::span<const uint8_t> bytes) override {Require(bytes.size()==4u*1024u*1024u && bytes[9]==7,"Initial VRAM copy changed");order.push_back(1);}
    void Reset() override{order.push_back(9);}
    void Gif(GifPathId path,std::span<const uint8_t> bytes) override {
        Require(bytes.size()==16 && bytes[9]==0x42,"Queued GIF retained recycled guest memory");order.push_back(10u+unsigned(path));
    }
    void Register(uint8_t address,uint64_t value) override {Require(address==7 && value==42,"Register changed");order.push_back(2);}
    void Image(const std::array<uint64_t,4>& registers,std::span<const uint8_t> bytes) override {setup=registers;image.assign(bytes.begin(),bytes.end());order.push_back(3);}
    size_t ReadFifo(std::span<uint8_t> bytes) override {
        if(beforeRead)beforeRead();
        if(failFifo)throw std::runtime_error("authored consumer failure");
        Require(order.back()==4,"FIFO overtook a VRAM update");std::fill(bytes.begin(),bytes.end(),0x3c);order.push_back(5);return bytes.size();
    }
    void Snapshot(std::vector<uint8_t>& bytes) override {bytes.assign(4u*1024u*1024u,0xa5);order.push_back(6);}
    void WriteVram(size_t offset,std::span<const uint8_t> bytes) override {Require(offset==16 && bytes.size()==16 && bytes[9]==0x42,"VRAM update changed");order.push_back(4);}
    void Frame(const cave::ps2::GsFrame& request) override {frame=request.vsyncTick;order.push_back(8);}
};
template<class Predicate> void Await(Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+2s;
    while(!predicate()) {
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Queue test handshake timed out");
        std::this_thread::sleep_for(1ms);
    }
}
void OwnedPayloadAndReplies() {
    GsCommandQueue queue;Consumer sink;
    auto producer=std::async(std::launch::async,[&] {
        std::vector<uint8_t> initial(4u*1024u*1024u);initial[9]=7;queue.Initialize(initial);
        std::array<uint8_t,16> payload{};payload[9]=0x42;
        for(auto path:{GifPathId::Path1,GifPathId::Path2,GifPathId::Path3})queue.Gif(path,payload);
        queue.Register(7,42);queue.Image({1,2,3,4},payload);queue.WriteVram(16,payload);
        payload.fill(0xff); // All queued spans must already belong to the queue.
        Require(queue.ReadFifo(payload)==16 && payload[9]==0x3c,"FIFO response changed");
        std::vector<uint8_t> snapshot;queue.Snapshot(snapshot);
        Require(snapshot.size()==4u*1024u*1024u && snapshot[9]==0xa5,"Snapshot response changed");
        cave::ps2::GsFrame request{};request.vsyncTick=123;queue.Frame(request);queue.Reset();
    });
    const auto deadline=std::chrono::steady_clock::now()+3s;
    while(producer.wait_for(0ms)!=std::future_status::ready) {
        queue.ConsumeOne(sink,10ms);
        if(std::chrono::steady_clock::now()>deadline){queue.Close();break;}
    }
    producer.get();
    Require(sink.order==std::vector<unsigned>{1,11,12,13,2,3,4,5,6,8,9},"Queue command order changed");
    Require(sink.frame==123 && sink.setup==std::array<uint64_t,4>{1,2,3,4} && sink.image[9]==0x42,"Frame or image snapshot changed");
    const auto stats=queue.GetStats();
    Require(!stats.commands && !stats.pendingBytes && !stats.frameOutstanding && stats.completed==stats.submitted,"Completed commands retained queue capacity");
    Require(stats.highWaterBytes<=8u*1024u*1024u && stats.highWaterCommands<=2048,"Queue exceeded its memory bound");
}
void BackpressureAndClose() {
    GsCommandQueue queue(32,2);Consumer sink;
    std::array<uint8_t,16> payload{};payload[9]=0x42;
    queue.Gif(GifPathId::Path1,payload);queue.Gif(GifPathId::Path2,payload);
    auto writer=std::async(std::launch::async,[&]{queue.Gif(GifPathId::Path3,payload);});
    Require(writer.wait_for(20ms)==std::future_status::timeout,"Full queue did not apply backpressure");
    Require(queue.ConsumeOne(sink,0ms),"Could not release one bounded command");
    Require(writer.wait_for(1s)==std::future_status::ready,"Writer did not resume after capacity was released");writer.get();
    Require(queue.GetStats().highWaterBytes==32 && queue.GetStats().highWaterCommands==2,"In-flight commands escaped capacity accounting");
    auto blocked=std::async(std::launch::async,[&]{try{queue.Gif(GifPathId::Path1,payload);return false;}catch(const std::runtime_error&){return true;}});
    Require(blocked.wait_for(20ms)==std::future_status::timeout,"Expected bounded writer to wait");
    queue.Close();Require(blocked.wait_for(1s)==std::future_status::ready && blocked.get(),"Close did not unblock a capacity waiter");
    Require(queue.GetStats().closed && !queue.ConsumeOne(sink,0ms),"Closed queue still executes commands");
}
void FrameAndFailure() {
    GsCommandQueue queue;Consumer sink;
    auto frame=std::async(std::launch::async,[&]{cave::ps2::GsFrame request{};request.vsyncTick=9;queue.Frame(request);});
    Await([&]{return queue.GetStats().frameOutstanding;});
    Require(frame.wait_for(0ms)==std::future_status::timeout,"Frame returned before consumer submission");
    queue.ConsumeOne(sink,0ms);frame.get();Require(sink.frame==9 && !queue.GetStats().frameOutstanding,"Frame barrier failed");
    sink.failFifo=true;
    auto reader=std::async(std::launch::async,[&]{std::array<uint8_t,16> reply{};try{queue.ReadFifo(reply);return false;}catch(const std::runtime_error& error){return std::string(error.what())=="authored consumer failure";}});
    Await([&]{return queue.GetStats().commands!=0;});
    bool threw=false;
    try{queue.ConsumeOne(sink,0ms);}catch(const std::runtime_error&){threw=true;}
    Require(threw && reader.wait_for(1s)==std::future_status::ready && reader.get(),"Consumer failure did not reach waiting producer");
    Require(queue.GetStats().closed,"Failed consumer left queue open");
}
void ClosePendingReadAndOwner() {
    GsCommandQueue queue;Consumer sink;
    queue.ConsumeOne(sink,0ms); // Bind this consumer, even on an empty queue.
    auto wrong=std::async(std::launch::async,[&]{try{queue.ConsumeOne(sink,0ms);return false;}catch(const std::logic_error&){return true;}});
    Require(wrong.get(),"Second thread consumed the GS queue");
    auto reader=std::async(std::launch::async,[&]{std::array<uint8_t,16> reply{};try{queue.ReadFifo(reply);return false;}catch(const std::runtime_error&){return true;}});
    Await([&]{return queue.GetStats().commands!=0;});queue.Close();
    Require(reader.wait_for(1s)==std::future_status::ready && reader.get(),"Shutdown stranded a synchronous read");
    GsCommandQueue tiny(8,1);std::array<uint8_t,16> payload{};bool rejected=false;
    try{tiny.Gif(GifPathId::Path1,payload);}catch(const std::length_error&){rejected=true;}
    Require(rejected,"Oversized command would wait forever");
}
void CloseActiveRead() {
    GsCommandQueue queue;Consumer sink;sink.order.push_back(4);
    std::promise<void> entered,release;
    auto didEnter=entered.get_future();auto canLeave=release.get_future();
    sink.beforeRead=[&]{entered.set_value();canLeave.wait();};
    auto reader=std::async(std::launch::async,[&]{std::array<uint8_t,16> bytes{};
        try{queue.ReadFifo(bytes);return false;}catch(const std::runtime_error&){return true;}});
    Await([&]{return queue.GetStats().commands!=0;});
    auto consumer=std::async(std::launch::async,[&]{return queue.ConsumeOne(sink,1s);});
    struct ReleaseOnExit {std::promise<void>& promise;~ReleaseOnExit(){try{promise.set_value();}catch(...){}}} guard{release};
    Require(didEnter.wait_for(1s)==std::future_status::ready,"Consumer did not enter readback");
    queue.Close();
    Require(reader.wait_for(1s)==std::future_status::ready && reader.get(),"Close stranded an in-flight read reply");
    release.set_value();Require(consumer.get(),"Active consumer did not unwind after close");
    Require(queue.GetStats().closed && !queue.GetStats().commands,"Closed in-flight accounting underflowed");
}
void NonblockingField() {
    GsCommandQueue queue(32,1);Consumer sink;unsigned wakes=0;
    queue.SetFrameNotifier([&]{++wakes;});
    queue.Register(7,42);cave::ps2::GsFrame first{};first.vsyncTick=1;
    Require(!queue.TryFrame(first),"Full command queue accepted an extra field");
    queue.ConsumeOne(sink,0ms);
    Require(wakes==1 && queue.TryFrame(first),"Capacity release did not wake a deferred field");
    auto second=first;second.vsyncTick=2;
    Require(!queue.TryFrame(second) && queue.GetStats().commands==1,"More than one field became outstanding");
    queue.ConsumeOne(sink,0ms);
    Require(sink.frame==1 && wakes==2 && queue.TryFrame(second),"Deferred field lost its first snapshot or completion wake");
    queue.ConsumeOne(sink,0ms);
    Require(sink.frame==2 && wakes==3 && queue.GetStats().submitted==3,"Field retry lost or duplicated a request");
}
}
int main() {
    try{OwnedPayloadAndReplies();BackpressureAndClose();FrameAndFailure();ClosePendingReadAndOwner();CloseActiveRead();NonblockingField();
        std::cout<<"GS queue owned payloads, order, bounds, frame barrier, failure and shutdown: PASS\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
