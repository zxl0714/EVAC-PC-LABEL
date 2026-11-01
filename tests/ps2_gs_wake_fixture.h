#pragma once
#include "ps2_gs_mmio_fixture.h"
#include "runtime/ee_scheduler.h"
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
}
