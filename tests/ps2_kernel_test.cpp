#include "ps2_runtime.h"
#include "sdk_binding.h"
#include "ps2_syscalls.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <algorithm>

// Inspect the actual event queue without a ROM or wall-clock sleep. A stalled
// guest must retain the same ordering in cycle and host time.
struct EeSchedulerClockTest {
  static void CheckDisplayFlip(bool waitForFlip, bool flipDuringBlank);
  static void Run(PS2Runtime& runtime) {
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    constexpr uint32_t callback=0x7ff000;
    require(runtime.registerFunction(callback,+[](uint8_t*,R5900Context*,PS2Runtime*){}),"Cannot register clock fixture");
    auto& scheduler=runtime.eeScheduler();R5900Context context{};context.pc=callback;
    scheduler.reset(runtime.memory().getRDRAM(),context);
    scheduler.m_hostClockOrigin-=std::chrono::seconds(2);
    scheduler.m_deadlines.clear();
    const uint64_t fieldCycles=(16667ull*EeScheduler::kEeClockHz+999999)/1000000;
    scheduler.scheduleEvent(fieldCycles,{EeEventType::VBlankStart,0,0});
    require(scheduler.setAlarm(1,callback,0,0,0)>0 && scheduler.setAlarm(100,callback,0,0,0)>0,
            "Clock fixture alarms rejected");
    auto events=scheduler.m_deadlines;
    std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.deadlineCycle<b.deadlineCycle;});
    require(events.size()==3 && events[0].hostDeadline<events[1].hostDeadline &&
            events[1].hostDeadline<events[2].hostDeadline,"Alarm host deadline inverted guest event order");
    require(events[2].hostDeadline<std::chrono::steady_clock::now(),"Guest stall restarted an alarm from host now");
    const auto previousOrigin=scheduler.m_hostClockOrigin;
    scheduler.reset(runtime.memory().getRDRAM(),context);
    require(scheduler.m_hostClockOrigin>previousOrigin && scheduler.m_deadlines.size()==1,
            "Reset retained old guest clock or events");

    scheduler.makeRunning(*scheduler.selectReady());
    scheduler.m_hostClockOrigin += std::chrono::seconds(1);
    unsigned reads = 0;
    auto read = [&](R5900Context& resumed) {
      require(resumed.pc == callback, "Timed pad read lost its guest continuation");
      ++reads;
    };
    try {
      scheduler.deferBeforeVBlank(3000, read);
    } catch (const EeDispatcherTransfer&) {}
    require(reads == 0 && !scheduler.currentThread() && scheduler.m_deadlines.size() == 2,
            "Early read must yield the EE thread until its sampling deadline");
    auto wake = scheduler.m_deadlines.back();
    require(wake.deadlineCycle < fieldCycles && wake.event.type == EeEventType::ExternalWake,
            "Timed read scheduled past the current field");
    scheduler.m_hostClockOrigin -= std::chrono::seconds(2);
    scheduler.m_eeCycle = wake.deadlineCycle;
    scheduler.processEvent(wake.event);
    auto* ready = scheduler.selectReady();
    require(ready && ready->resumeCompletion, "Timed input did not wake its caller");
    scheduler.makeRunning(*ready);
    std::move(ready->resumeCompletion)(ready->activeContext());
    scheduler.deferBeforeVBlank(3000, read);
    require(reads == 2 && scheduler.m_deadlines.size() == 2,
            "A missed sampling deadline must read immediately");
  }
};

namespace {
void Require(bool value,const char* message) {
  if(!value)throw std::runtime_error(message);
}
int32_t Result(const R5900Context& context) {
  return static_cast<int32_t>(getRegU32(&context,2));
}
void BootEntry(uint8_t*,R5900Context*,PS2Runtime*) {}
void ModuleEntry(uint8_t*,R5900Context*,PS2Runtime*) {}
void OtherModuleEntry(uint8_t*,R5900Context*,PS2Runtime*) {}
void YieldAtEntry(uint8_t*,R5900Context* context,PS2Runtime*) {
  context->pc=0x100200;
}
void IopHeapLifecycle(PS2Runtime& runtime) {
  const auto allocate=cave::ps2::SdkHandler("sceSifAllocIopHeap");
  const auto release=cave::ps2::SdkHandler("sceSifFreeIopHeap");
  const auto releaseSys=cave::ps2::SdkHandler("sceSifFreeSysMemory");
  Require(allocate && release && releaseSys,"IOP heap SDK lifecycle handlers missing");
  R5900Context context{};
  uint32_t first{};
  for(unsigned cycle=0;cycle<128;++cycle) {
    SET_GPR_U32(&context,4,0x40000u);
    allocate(nullptr,&context,&runtime);
    const auto address=getRegU32(&context,2);
    Require(address && (!first || first==address),"Sound-bank reload leaked its IOP staging buffer");
    first=address;
    SET_GPR_U32(&context,4,address);
    (cycle&1?releaseSys:release)(nullptr,&context,&runtime);
    Require(Result(context)==0,"Sound-bank staging buffer did not return to the common IOP heap");
  }
}
void ExplicitIoRoots(PS2Runtime& runtime) {
  const auto before=PS2Runtime::getIoPaths();
  auto requested=before;
  const auto temporary=std::filesystem::temp_directory_path()/"evac-authored-no-disc-fixture";
  requested.elfDirectory=temporary/"prepared";requested.elfPath=requested.elfDirectory/"missing.elf";
  requested.hostRoot=temporary/"readonly";requested.cdRoot=requested.hostRoot;
  requested.mcRoot=temporary/"user-data/mc0";
  PS2Runtime::setIoPaths(requested);
  Require(!runtime.loadELF(requested.elfPath.string()),"Missing authored test ELF unexpectedly exists");
  const auto after=PS2Runtime::getIoPaths();
  Require(after.hostRoot==requested.hostRoot && after.cdRoot==requested.cdRoot && after.mcRoot==requested.mcRoot,
          "ELF loading replaced explicit read-only data or profile roots");
  PS2Runtime::setIoPaths(before);
}
void LoadOtherModule(uint8_t* memory,R5900Context* context,PS2Runtime*) {
  const uint32_t other=17;
  std::memcpy(memory+0x200004,&other,4);
  context->pc=getRegU32(context,31);
}
}

#include "ps2_vif_transport_fixture.h"
#include "ps2_gs_mmio_fixture.h"
#include "ps2_gs_wake_fixture.h"
#include "ps2_gs_sdk_fixture.h"
#include "ps2_timer_fixture.h"
#include "ps2_dma_chain_fixture.h"
#include "ps2_memcard_fixture.h"

void EeSchedulerClockTest::CheckDisplayFlip(bool waitForFlip, bool flipDuringBlank) {
  auto runtime=std::make_unique<PS2Runtime>();
  Require(runtime->memory().initialize() && runtime->syncCoreSubsystems(),"Cannot initialize scanout fixture");
  struct Sink final : GsMmioBackend {
    bool busy=false;
    std::vector<cave::ps2::GsFrame> frames;
    bool TryFrame(const cave::ps2::GsFrame& frame) override {
      if(busy)return false;
      frames.push_back(frame);
      return true;
    }
  };
  auto backend=std::make_unique<Sink>();
  auto* sink=backend.get();
  runtime->gs().setCommandBackend(std::move(backend));
  auto& scheduler=runtime->eeScheduler();
  scheduler.setScanoutAfterDisplayFlip(waitForFlip);
  scheduler.reset(runtime->memory().getRDRAM(),{});
  scheduler.m_hostClockOrigin-=std::chrono::seconds(1);
  // No guest executes here. Advance the two edges separately and flip DISPFB
  // between them, just as a game responding to the start interrupt would.
  const auto start=scheduler.m_deadlines.front().deadlineCycle;
  scheduler.m_deadlines.clear();
  scheduler.scheduleEvent(start,{EeEventType::VBlankStart,0,0});
  const auto oldOrigin=scheduler.m_hostClockOrigin;
  scheduler.scheduleEvent(5*start,{EeEventType::ExternalWake,0,0});
  const auto oldWake=scheduler.m_deadlines.back().hostDeadline;
  runtime->memory().gs().pmode=2;
  runtime->memory().gs().dispfb2=0x1400;
  scheduler.m_eeCycle=start;
  scheduler.processDueDeadlines();
  const auto shift=scheduler.m_hostClockOrigin-oldOrigin;
  auto wake=std::find_if(scheduler.m_deadlines.begin(),scheduler.m_deadlines.end(),
      [](const auto& entry){return entry.event.type==EeEventType::ExternalWake;});
  Require(shift>std::chrono::milliseconds(900) && wake!=scheduler.m_deadlines.end() &&
          wake->deadlineCycle==5*start && wake->hostDeadline-oldWake==shift,
          "A host stall must rebase all future deadlines without skipping guest time");
  Require(scheduler.currentVSyncTick()==1 && sink->frames.size()==(waitForFlip?0u:1u),"Wrong start-of-blank latch");
  if(flipDuringBlank)runtime->memory().gs().dispfb2=0x1446;
  scheduler.processPendingEvents();
  Require(sink->frames.size()==(!waitForFlip || flipDuringBlank?1u:0u),"Did not use the completed display flip");
  sink->busy=true;
  const auto end=std::min_element(scheduler.m_deadlines.begin(),scheduler.m_deadlines.end(),
      [](const auto& a,const auto& b){return a.deadlineCycle<b.deadlineCycle;});
  scheduler.m_eeCycle=end->deadlineCycle;
  scheduler.processDueDeadlines();
  Require(scheduler.m_deferredVBlank.has_value()==(waitForFlip && !flipDuringBlank),"Busy field was lost or deferred twice");
  sink->busy=false;
  scheduler.processDueDeadlines();
  Require(!scheduler.m_deferredVBlank && sink->frames.size()==1 && sink->frames[0].vsyncTick==1 &&
          sink->frames[0].field==1 && sink->frames[0].dispfb2==(waitForFlip && flipDuringBlank?0x1446u:0x1400u),
          "Wrong framebuffer, field parity or duplicate after scanout retry");
  Require(scheduler.m_deadlines.size()==2 && std::any_of(scheduler.m_deadlines.begin(),scheduler.m_deadlines.end(),
              [start](const auto& event){return event.event.type==EeEventType::VBlankStart && event.deadlineCycle==2*start;}),
          "Changing scanout shifted the next VBlank");
}
int main(int argc,char** argv) {
  try {
    if(argc==5 && std::string_view(argv[1])=="--replay-dma") {
      ReplayPrivateDma(argv[2],argv[3],argv[4]);return 0;
    }
    PS2Runtime runtime;
    if(argc==2 && std::string_view(argv[1])=="--timer-bench")return EeTimerBench(runtime.memory());
    if(argc!=1)throw std::runtime_error("Unknown kernel test option");
    EeSchedulerClockTest::Run(runtime);
    EeSchedulerClockTest::CheckDisplayFlip(false,true);
    EeSchedulerClockTest::CheckDisplayFlip(true,true);
    EeSchedulerClockTest::CheckDisplayFlip(true,false);
    DmaLongChain();
    EeTimerArithmetic(runtime.memory());
    EeTimerDeferredEquivalence();
    ExplicitIoRoots(runtime);
    std::vector<uint8_t> memory(PS2_RAM_SIZE);
    R5900Context context{};
    context.pc=0x100000;
    auto& scheduler=runtime.eeScheduler();
    scheduler.reset(memory.data(),context);
    SET_GPR_U32(&context,4,EeScheduler::kMainThreadId);
    SET_GPR_U32(&context,5,0x2000);
    ps2_syscalls::ReferThreadStatus(memory.data(),&context,&runtime);
    Require(Result(context)==0,"Main thread status failed");
    uint32_t inherited{};
    std::memcpy(&inherited,memory.data()+0x2018,4);
    Require(inherited==0,"Expected EE boot priority zero");
    // EE ABI: status, entry, stack, size, gp, initial priority, current
    // priority, attributes, option. A new thread inherits initial priority.
    std::array<uint32_t,9> descriptor{0,0x100100,0x10000,0x1000,0x200000,
                                     inherited,99,0,0};
    auto create=[&](int priority) {
      descriptor[5]=static_cast<uint32_t>(priority);
      std::memcpy(memory.data()+0x1000,descriptor.data(),sizeof(descriptor));
      SET_GPR_U32(&context,4,0x1000);
      ps2_syscalls::CreateThread(memory.data(),&context,&runtime);
      return Result(context);
    };
    const auto worker=create(static_cast<int>(inherited));
    Require(worker>EeScheduler::kMainThreadId,"Inherited priority zero rejected");
    Require(scheduler.thread(worker)->initialPriority==0 &&
            scheduler.thread(worker)->currentPriority==0,"Priority ABI lost");
    Require(create(127)>0,"Priority 127 rejected");
    Require(create(-1)==-403 && create(128)==-403,"Invalid priority accepted");
    int previous{};
    Require(scheduler.changePriority(worker,127,false,previous)==0 && previous==0,
            "Valid priority change failed");
    Require(scheduler.changePriority(worker,0,false,previous)==0 && previous==127,
            "Priority zero change rejected");
    Require(scheduler.changePriority(worker,-1,false,previous)==-403 &&
            scheduler.changePriority(worker,128,false,previous)==-403 &&
            scheduler.thread(worker)->currentPriority==0,"Invalid change mutated thread");
    SET_GPR_U32(&context,4,EeScheduler::kMainThreadId);
    SET_GPR_U32(&context,5,24);
    ps2_syscalls::ChangeThreadPriority(memory.data(),&context,&runtime);
    Require(Result(context)==0,"First priority change did not return boot priority");
    SET_GPR_U32(&context,5,1);
    ps2_syscalls::iChangeThreadPriority(memory.data(),&context,&runtime);
    const auto saved=Result(context);
    Require(saved==24,"Critical section did not receive caller priority");
    SET_GPR_U32(&context,5,saved);
    ps2_syscalls::iChangeThreadPriority(memory.data(),&context,&runtime);
    Require(Result(context)==1 && scheduler.thread(1)->currentPriority==24,
            "Critical section failed to restore caller priority");
    SET_GPR_U32(&context,5,128);
    ps2_syscalls::iChangeThreadPriority(memory.data(),&context,&runtime);
    Require(Result(context)==-403 && scheduler.thread(1)->currentPriority==24,
            "Failed syscall lost error or changed priority");
    SET_GPR_U32(&context,4,0x35fcf0);
    SET_GPR_U32(&context,5,0xffffffffu);
    SET_GPR_U32(&context,6,0x106000);
    SET_GPR_U32(&context,7,0x3000);
    const auto previousPaths=PS2Runtime::getIoPaths();
    auto bootPaths=previousPaths;bootPaths.elfPath="fixture/boot.elf";
    PS2Runtime::setIoPaths(bootPaths);
    ps2_syscalls::SetupThread(memory.data(),&context,&runtime);
    PS2Runtime::setIoPaths(previousPaths);
    std::array<uint32_t,3> bootArguments{};
    std::memcpy(bootArguments.data(),memory.data()+0x3000,sizeof(bootArguments));
    Require(bootArguments==std::array<uint32_t,3>{1u,0x300cu,0u} &&
            std::strcmp(reinterpret_cast<const char*>(memory.data()+bootArguments[1]),"boot.elf")==0,
            "EE boot argc/argv omitted the executable or final null pointer");
    SET_GPR_U32(&context,7,PS2_RAM_SIZE-8);
    bool invalidArguments=false;
    try{ps2_syscalls::SetupThread(memory.data(),&context,&runtime);}
    catch(const std::runtime_error&){invalidArguments=true;}
    Require(invalidArguments,"Out-of-range EE boot argument block was accepted");
    SET_GPR_U32(&context,7,PS2_RAM_SIZE+0x3000u);
    invalidArguments=false;
    try{ps2_syscalls::SetupThread(memory.data(),&context,&runtime);}
    catch(const std::runtime_error&){invalidArguments=true;}
    Require(invalidArguments,"Invalid physical argument address wrapped into RDRAM");
    SET_GPR_U32(&context,7,0);
    ps2_syscalls::SetupThread(memory.data(),&context,&runtime);
    const auto mainTop=PS2_RAM_SIZE-0x1000u;
    Require(getRegU32(&context,2)==mainTop-0x2a0u && scheduler.thread(1)->stack==mainTop-0x106000 &&
            scheduler.thread(1)->stackSize==0x106000,"Automatic stack top or allocation metadata wrong");
    SET_GPR_U32(&context,4,0xd30800);SET_GPR_U32(&context,5,0xffffffffu);
    Require(ps2_syscalls::dispatchNumericSyscall(0x3d,memory.data(),&context,&runtime),
            "SetupHeap syscall was not dispatched");
    Require(runtime.guestHeapLimit()==scheduler.thread(1)->stack,
            "Heap ceiling overlaps automatic main stack");
    Require(getRegU32(&context,2)==runtime.guestHeapLimit(),"SetupHeap returned the wrong boundary");
    SET_GPR_U32(&context,29,mainTop-0x200);
    SET_GPR_U32(&context,4,1);SET_GPR_U32(&context,5,0x2000);
    ps2_syscalls::ReferThreadStatus(memory.data(),&context,&runtime);
    uint32_t stackBase{};std::memcpy(&stackBase,memory.data()+0x2008,4);
    Require(stackBase==mainTop-0x106000,"Live SP changed reported stack allocation");
    const auto callbackTop=runtime.reserveAsyncCallbackStack(0x4000,16);
    Require(callbackTop>=0x90000+0x4000-16 && callbackTop<0x100000,
            "Callback stack aliases the game heap or main stack");
    Require(runtime.memory().initialize(),"Headless memory initialization failed");
    IopHeapLifecycle(runtime);
    runtime.configureGuestHeap(0x400000,0x410000);
    const auto arena=runtime.guestMalloc(0xf000);
    Require(arena!=0,"Large SDK allocation failed within the heap");
    runtime.guestFree(arena);
    Require(runtime.guestMalloc(0xfff0)==0 && runtime.guestMalloc(1)!=0,
            "Heap capacity probe consumed its bookkeeping reserve or leaked a failed allocation");
    runtime.configureGuestHeap(0x400000,0x410000);
    const auto native=runtime.guestMalloc(128,64);
    auto* heapMemory=runtime.memory().getRDRAM();
    std::memset(heapMemory+native,0x5a,128);
    const auto mallocHandler=cave::ps2::SdkHandler("_malloc_r");
    const auto freeHandler=cave::ps2::SdkHandler("_free_r");
    const auto callocHandler=cave::ps2::SdkHandler("_calloc_r");
    const auto alignedHandler=cave::ps2::SdkHandler("memalign");
    if(!mallocHandler || !freeHandler || !callocHandler)
      throw std::runtime_error("Guest allocator ABI handlers missing");
    context.r[4]=_mm_cvtsi32_si128(0x298a88);
    context.r[5]=_mm_cvtsi32_si128(256);
    mallocHandler(heapMemory,&context,&runtime);
    const uint32_t guest=uint32_t(_mm_cvtsi128_si32(context.r[2]));
    if(!native || guest<native+128 || guest+256>0x410000)
      throw std::runtime_error("Kernel and guest allocations overlap");
    std::memset(heapMemory+guest,0x3c,256);
    context.r[5]=_mm_cvtsi32_si128(guest);
    freeHandler(heapMemory,&context,&runtime);
    context.r[5]=_mm_cvtsi32_si128(16);context.r[6]=_mm_cvtsi32_si128(16);
    callocHandler(heapMemory,&context,&runtime);
    const uint32_t reused=uint32_t(_mm_cvtsi128_si32(context.r[2]));
    if(reused!=guest)throw std::runtime_error("Guest free did not return its allocation to the shared heap");
    for(unsigned i=0;i<128;++i)
      if(heapMemory[native+i]!=0x5a)throw std::runtime_error("Guest allocator corrupted kernel-owned data");
    for(unsigned i=0;i<256;++i)
      if(heapMemory[reused+i]!=0)throw std::runtime_error("Guest calloc did not clear reused memory");
    context.r[4]=_mm_cvtsi32_si128(128);context.r[5]=_mm_cvtsi32_si128(4096);
    if(!alignedHandler)throw std::runtime_error("Aligned allocation ABI handler missing");
    alignedHandler(heapMemory,&context,&runtime);
    const uint32_t aligned=uint32_t(_mm_cvtsi128_si32(context.r[2]));
    Require(aligned!=0 && (aligned&127)==0 && aligned>=guest+256,
            "Aligned allocation used a different heap or lost DMA alignment");
    const std::array<PS2Runtime::RecompiledFunction,2> table{ModuleEntry,nullptr};
    runtime.registerStaticModule({16,0x200000,64,16,16,0x200040,table});
    Require(runtime.registerFunction(0x100000,BootEntry),"Boot function registration failed");
    descriptor[1]=0x100000;
    const auto started=create(127);
    SET_GPR_U32(&context,4,started);SET_GPR_U32(&context,5,0x12345678u);
    ps2_syscalls::StartThread(memory.data(),&context,&runtime);
    Require(Result(context)==started && scheduler.thread(started)->arg==0x12345678u,
            "EE StartThread must return its thread ID on success");
    SET_GPR_U32(&context,4,started);
    ps2_syscalls::StartThread(memory.data(),&context,&runtime);
    Require(Result(context)==-414,"Starting an active EE thread lost its error result");
    Require(runtime.hasFunction(0x100000) && !runtime.hasFunction(0x200040),
            "Unloaded bank affected boot lookup");
    const std::array<uint32_t,6> moduleHeader{0x336f574d,16,0x200000,64,16,16};
    std::memcpy(runtime.memory().getRDRAM()+0x200000,moduleHeader.data(),sizeof(moduleHeader));
    Require(runtime.hasFunction(0x200040) && runtime.lookupFunction(0x200040)==ModuleEntry &&
            runtime.lookupFunction(0x100000)==BootEntry && !runtime.hasFunction(0x200044),
            "Runtime module dispatch lost selected bank, boot entry or sparse slot");
    Require(runtime.registerFunction(0x100200,YieldAtEntry),"Yield fixture registration failed");
    Require(!runtime.dispatchGuestBranch(runtime.memory().getRDRAM(),&context,0x100200,
              0x100300,0x100308,PS2Runtime::GuestBranchKind::DirectCall,"JAL") &&
            context.pc==0x100200,"A callee yielding at its entry was treated as a completed call");
    const std::array<PS2Runtime::RecompiledFunction,2> otherTable{OtherModuleEntry,nullptr};
    runtime.registerStaticModule({17,0x200000,64,16,16,0x200040,otherTable});
    Require(runtime.registerFunction(0x100100,LoadOtherModule),"Loader fixture registration failed");
    SET_GPR_U32(&context,31,0x200048);
    Require(!runtime.dispatchGuestBranch(runtime.memory().getRDRAM(),&context,0x100100,
              0x200044,0x200048,PS2Runtime::GuestBranchKind::DirectCall,"JAL") &&
            context.pc==0x200048 && runtime.lookupFunction(0x200040)==OtherModuleEntry,
            "Native caller continued after its guest code bank was replaced");
    MemoryCardHandles(runtime);
    VifImageContinuation(runtime.memory());
    GsMmioCompletions(runtime.memory());
    GsSdkDisplay(runtime);
    GsIdleWake();
    GsFrameSignalProgress();
    std::cout<<"PS2 kernel: EE ABI, priority inheritance, boundaries, restore and raw VIF transport passed\n";
    return 0;
  }catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
  }
}
