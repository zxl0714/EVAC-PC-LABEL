#pragma once
#include "ps2_gs_mmio_fixture.h"
#include "runtime/ee_scheduler.h"
#include "gs_command_queue.h"
#include "gs_completion_mailbox.h"
#include <atomic>
#include <chrono>
#include <thread>

namespace {
std::atomic<bool> gsWakeSleeping=false;
std::atomic<unsigned> gsWakeHandled=0;
void GsWakeEntry(uint8_t*,R5900Context*,PS2Runtime* runtime) {
    auto& scheduler=runtime->eeScheduler();
    scheduler.addIrqHandler(false,0,0x100004,true,0,0,0);
    scheduler.setIrqCauseEnabled(false,0,true);
    gsWakeSleeping.store(true,std::memory_order_release);
    scheduler.sleepCurrent();
}
void GsWakeInterrupt(uint8_t*,R5900Context* context,PS2Runtime* runtime) {
    gsWakeHandled.fetch_add(1,std::memory_order_relaxed);
    runtime->eeScheduler().requestStop();context->pc=0;
}
void GsIdleWake() {
    using namespace std::chrono_literals;
    // This fixture creates a second runtime inside the kernel test. Keep its
    // large debug/VU state off the executable's default 1 MiB Windows stack.
    auto storage=std::make_unique<PS2Runtime>();auto& runtime=*storage;
    Require(runtime.memory().initialize(),"Cannot initialize GS wake fixture memory");
    runtime.gs().init(runtime.memory().getGSVRAM(),uint32_t(PS2_GS_VRAM_SIZE),&runtime.memory().gs());
    auto backend=std::make_unique<GsMmioBackend>();auto* record=backend.get();runtime.gs().setCommandBackend(std::move(backend));
    Require(runtime.registerFunction(0x100000,GsWakeEntry) && runtime.registerFunction(0x100004,GsWakeInterrupt),"Cannot register GS wake fixture");
    runtime.memory().gs().csr=0;runtime.memory().gs().imr=0;
    R5900Context context{};context.pc=0x100000;
    auto& scheduler=runtime.eeScheduler();scheduler.reset(runtime.memory().getRDRAM(),context);
    Require(scheduler.isExecutorThread(),"EE owner token was not bound on reset");
    gsWakeSleeping=false;gsWakeHandled=0;std::atomic<bool> foreignRejected=false;
    std::jthread worker([&]{
        foreignRejected=!scheduler.isExecutorThread();
        const auto deadline=std::chrono::steady_clock::now()+3s;
        while(!gsWakeSleeping.load(std::memory_order_acquire)) {
            if(std::chrono::steady_clock::now()>deadline){scheduler.requestStop();return;}
            std::this_thread::sleep_for(1ms);
        }
        record->Publish({1,0,0,false,true,false});
        scheduler.postEvent({EeEventType::GsStatus,0,0});
    });
    scheduler.run();worker.join();
    Require(foreignRejected && gsWakeHandled==1 && (runtime.memory().gs().csr.load()&2),"GS event did not wake idle EE and dispatch one native IRQ");
}

std::atomic<unsigned> gsFrameSignals=0;
void GsFrameEntry(uint8_t*,R5900Context* context,PS2Runtime* runtime) {
    auto& scheduler=runtime->eeScheduler();
    scheduler.addIrqHandler(false,0,0x100004,true,0,0,0);
    scheduler.setIrqCauseEnabled(false,0,true);
    runtime->gs().writeRegister(0x60,0xffffffff00000001ull);
    runtime->gs().writeRegister(0x60,0xffffffff00000002ull);
    context->pc=0x100008;scheduler.waitVSync(0);
}
void GsFrameInterrupt(uint8_t*,R5900Context* context,PS2Runtime* runtime) {
    gsFrameSignals.fetch_add(1,std::memory_order_relaxed);
    runtime->memory().write32(0x12001000,1);
    context->pc=0;
}
void GsFrameIdle(uint8_t*,R5900Context* context,PS2Runtime* runtime) {
    context->pc=0x100008;runtime->eeScheduler().sleepCurrent();
}
void GsFrameSignalProgress() {
    using namespace std::chrono_literals;
    auto storage=std::make_unique<PS2Runtime>();auto& runtime=*storage;
    Require(runtime.memory().initialize() && runtime.syncCoreSubsystems(),"Cannot initialize GS frame/IRQ fixture");
    auto& scheduler=runtime.eeScheduler();
    auto mailbox=std::make_shared<cave::ps2::GsCompletionMailbox>();
    auto transport=std::make_unique<cave::ps2::GsCommandQueue>(8u*1024u*1024u,2048,mailbox);
    auto* queue=transport.get();
    class SignalSink final : public GsMmioBackend {
    public:
        PS2Runtime& runtime;cave::ps2::GsCommandQueue& queue;
        std::shared_ptr<cave::ps2::GsCompletionMailbox> mailbox;
        std::vector<cave::ps2::GsFrame> frames;
        uint64_t generation=0;
        SignalSink(PS2Runtime& r,cave::ps2::GsCommandQueue& q,std::shared_ptr<cave::ps2::GsCompletionMailbox> m)
            :runtime(r),queue(q),mailbox(std::move(m)){}
        void BeginCommand(uint64_t value) override{generation=value;}
        void Register(uint8_t address,uint64_t value) override {
            Require(address==0x60,"Unexpected GS frame fixture register");
            // Publish the first SIGNAL only once VBlank has queued a frame
            // behind it. A synchronous EE frame wait then cannot ACK it.
            while(!queue.GetStats().frameOutstanding && !queue.GetStats().closed)std::this_thread::sleep_for(1ms);
            mailbox->Signal(generation,value);
        }
        void Frame(const cave::ps2::GsFrame& frame) override {
            frames.push_back(frame);
            // Hold one field beyond the next VBlank: that VBlank must defer,
            // then wake on completion without losing or duplicating a field.
            if(frames.size()==1)std::this_thread::sleep_for(40ms);
            else runtime.requestStop();
        }
    } sink(runtime,*queue,mailbox);
    Require(runtime.registerFunction(0x100000,GsFrameEntry) && runtime.registerFunction(0x100004,GsFrameInterrupt) &&
            runtime.registerFunction(0x100008,GsFrameIdle),"Cannot register GS frame fixture");
    runtime.memory().gs().csr=0;runtime.memory().gs().imr=0;
    R5900Context context{};context.pc=0x100000;scheduler.reset(runtime.memory().getRDRAM(),context);
    gsFrameSignals=0;std::atomic<bool> done=false,timedOut=false;std::exception_ptr consumerFailure;
    mailbox->SetNotifier([&]{scheduler.postEvent({EeEventType::GsStatus,0,0});});
    queue->SetFrameNotifier([&]{scheduler.postEvent({EeEventType::GsStatus,0,0});});
    std::jthread consumer([&]{try{while(!done)queue->ConsumeOne(sink,2ms);}catch(...){consumerFailure=std::current_exception();scheduler.requestStop();}});
    std::jthread watchdog([&]{const auto deadline=std::chrono::steady_clock::now()+2s;
        while(!done && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(2ms);
        if(!done){timedOut=true;scheduler.requestStop();queue->Close();}});
    std::exception_ptr executionFailure;
    try{runtime.gs().setCommandBackend(std::move(transport));scheduler.run();}catch(...){executionFailure=std::current_exception();}
    done=true;queue->Close();consumer.join();watchdog.join();
    queue->SetFrameNotifier({});mailbox->SetNotifier({});
    Require(!timedOut,"EE VBlank blocked its own GS SIGNAL acknowledgement");
    if(executionFailure)std::rethrow_exception(executionFailure);
    if(consumerFailure)std::rethrow_exception(consumerFailure);
    Require(gsFrameSignals==2 && sink.frames.size()==2 && sink.frames[0].vsyncTick==1 &&
            sink.frames[1].vsyncTick==2 && sink.frames[0].field==1 && sink.frames[1].field==0 &&
            runtime.memory().gs().vsyncTick.load()==2,"Deferred GS field changed ticks/parity or lost an IRQ");
}
}
