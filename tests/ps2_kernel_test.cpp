#include "ps2_runtime.h"
#include "native_host.h"
#include "sdk_binding.h"
#include "ps2_syscalls.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "ps2_iso_fixture.h"
#include "cd_state.h"
#include "vu_state.h"
#include "Stubs/CD.h"
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include <algorithm>

// Inspect the actual event queue without a ROM or wall-clock sleep. A stalled
// guest must retain the same ordering in cycle and host time.
struct EeSchedulerClockTest {
  static void CheckDiscWait(PS2Runtime& runtime);
  static void CheckCallbackStacks();
  static void CheckExternalPause(bool sleeping);
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
    static unsigned reads;
    reads = 0;
    scheduler.setDeferredPadRead(+[](uint8_t*, R5900Context* resumed, PS2Runtime*) {
      if (resumed->pc != callback) throw std::runtime_error("Timed pad read lost its guest continuation");
      ++reads;
    });
    try {
      scheduler.deferBeforeVBlank(3000);
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
    require(ready && std::holds_alternative<EeDeferredPadRead>(ready->resumeCompletion), "Timed input did not wake its caller");
    scheduler.makeRunning(*ready);
    scheduler.resumeWait(ready->resumeCompletion, ready->activeContext());
    ready->resumeCompletion = {};
    scheduler.deferBeforeVBlank(3000);
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
void FloatToWord() {
  Require(FPU_CVT_W_S(100.0f / (100.1f / 5.0f)) == 4,
          "R-Type Final full charge crossed the last wave-cannon threshold");
  Require(FPU_CVT_W_S(-4.99f) == -4 && FPU_CVT_W_S(0.99f) == 0,
          "EE float conversion must truncate toward zero");
  for (const uint32_t magnitude : {0x4f000000u, 0x7f800000u, 0x7fc00001u}) {
    Require(FPU_CVT_W_S(std::bit_cast<float>(magnitude)) == INT32_MAX &&
            FPU_CVT_W_S(std::bit_cast<float>(magnitude | 0x80000000u)) == INT32_MIN,
            "EE float conversion lost signed saturation");
  }
}
void VuPacketRestore(PS2Runtime& runtime) {
  std::array<uint32_t, 64> code{};
  for (size_t i = 0; i < code.size(); i += 2) {
    code[i] = 0x8000033cu;  // lower NOP
    code[i + 1] = 0x000002ffu;  // upper NOP
  }
  code[0] = 0x800006fcu;  // XGKICK vi0
  std::array<uint8_t, 256> data{};
  const uint64_t tag = (2ull << 58) | (1u << 15) | 7u;
  std::memcpy(data.data(), &tag, sizeof(tag));
  for (size_t i = 16; i < 128; ++i) data[i] = static_cast<uint8_t>(i);
  std::vector<std::vector<uint8_t>> packets;
  const auto observe = [&](const uint8_t* p, uint32_t n) { packets.emplace_back(p, p + n); };
  auto vu = std::make_unique<VU1Interpreter>();
  vu->setPacketObserver(observe);
  auto* bytes = reinterpret_cast<uint8_t*>(code.data());
  vu->execute(bytes, sizeof(code), data.data(), data.size(), runtime.gs(), nullptr, 0, 0, 0, 6);
  Require(packets.empty(), "XGKICK completed before its payload was transferred");
  const auto midPacket = cave::ps2::VuStateArchive::Save(*vu);
  // VU code can overwrite the start of the packet after XGKICK has read it.
  // A restore that restarts the transfer from RAM would now read a different tag.
  std::fill_n(data.begin(), 16, uint8_t{0});
  data[127] = 0xfe;
  vu->resume(bytes, sizeof(code), data.data(), data.size(), runtime.gs(), nullptr, 0, 0, 20);
  Require(packets.size() == 1 && packets[0].size() == 128 && packets[0][0] == 7 &&
          packets[0].back() == 0xfe, "XGKICK failed to retain a consumed tag or skipped a live source write");
  auto cold = std::make_unique<VU1Interpreter>();
  cold->setPacketObserver(observe);
  cave::ps2::VuStateArchive::Load(*cold, midPacket);
  cold->resume(bytes, sizeof(code), data.data(), data.size(), runtime.gs(), nullptr, 0, 0, 20);
  Require(packets.size() == 2 && packets[1] == packets[0], "Cold VU restore emitted a different GIF packet");
}
void VuMicrocodeUpdates(PS2Runtime& runtime) {
  constexpr uint32_t lowerNop=0x8000033c, upperNop=0x000002ff, end=0x40000000;
  constexpr uint32_t operands=(8u<<21)|(2u<<16)|(1u<<11)|(3u<<6);
  std::array<uint32_t,6> code{lowerNop,operands|0x28u,lowerNop,upperNop|end,lowerNop,upperNop};
  std::array<uint8_t,16> data{};
  auto vu=std::make_unique<VU1Interpreter>();
  auto* bytes=reinterpret_cast<uint8_t*>(code.data());
  const auto size=static_cast<uint32_t>(sizeof(code));
  vu->state().vf[1][0]=3.0f;
  vu->state().vf[2][0]=1.0f;
  vu->execute(bytes,size,data.data(),data.size(),runtime.gs(),nullptr,0,0,0,1);
  Require(vu->state().vf[3][0]==0.0f,"VU register became visible before writeback");
  const auto duringAdd = cave::ps2::VuStateArchive::Save(*vu);
  vu->resume(bytes,size,data.data(),data.size(),runtime.gs());
  Require(vu->state().vf[3][0]==4.0f,"VU resume lost a pending write");
  const auto afterAdd = cave::ps2::VuStateArchive::Save(*vu);
  auto truncated = duringAdd;
  truncated.pop_back();
  bool rejected = false;
  try { cave::ps2::VuStateArchive::Load(*vu, truncated); }
  catch (const std::invalid_argument&) { rejected = true; }
  Require(rejected && cave::ps2::VuStateArchive::Save(*vu) == afterAdd,
          "Incomplete VU restore changed the running program");
  auto cold = std::make_unique<VU1Interpreter>();
  cave::ps2::VuStateArchive::Load(*cold, duringAdd);
  Require(cold->state().vf[3][0] == 0.0f, "VU restore committed the pending result early");
  cold->resume(bytes,size,data.data(),data.size(),runtime.gs());
  Require(cave::ps2::VuStateArchive::Save(*cold) == afterAdd,
          "Cold VU restore diverged after register writeback");
  code[1]=operands|0x2cu;
  vu->execute(bytes,size,data.data(),data.size(),runtime.gs());
  Require(vu->state().vf[3][0]==2.0f && vu->state().vf[3][1]==0.0f,
          "VU decode retained a replaced instruction or changed inactive lanes");
  vu->state().vf[1][0]=1.0f;
  vu->state().vf[2][0]=3.0f;
  vu->execute(bytes,size,data.data(),data.size(),runtime.gs());
  Require(vu->state().vf[3][0]==-2.0f && (vu->state().mac&0x0080u)!=0,
          "VU normal negative result lost its MAC sign flag");
  code[1]=operands|0x28u;
  const auto largest=std::numeric_limits<float>::max();
  vu->state().vf[1][0]=largest;
  vu->state().vf[2][0]=largest;
  vu->execute(bytes,size,data.data(),data.size(),runtime.gs());
  Require(vu->state().vf[3][0]==largest && (vu->state().mac&0x8000u)!=0,
          "VU overflow did not saturate and set the MAC overflow flag");
  constexpr uint32_t inputs=(8u<<21)|(2u<<16)|(1u<<11);
  std::array<uint32_t,8> accumulate{lowerNop,inputs|0x2beu,lowerNop,operands|0x29u,
                                    lowerNop,upperNop|end,lowerNop,upperNop};
  vu->state().vf[1][0]=-3.0f;
  vu->state().vf[2][0]=2.0f;
  vu->execute(reinterpret_cast<uint8_t*>(accumulate.data()),sizeof(accumulate),
              data.data(),data.size(),runtime.gs());
  Require(vu->state().acc[0]==-6.0f && vu->state().vf[3][0]==-12.0f && (vu->state().mac&0x0080u)!=0,
          "VU multiply-accumulate lost the forwarded accumulator or its flag");
}
void BootEntry(uint8_t*,R5900Context*,PS2Runtime*) {}
void ModuleEntry(uint8_t*,R5900Context*,PS2Runtime*) {}
void OtherModuleEntry(uint8_t*,R5900Context*,PS2Runtime*) {}
void DiscExecutableSwitch() {
  constexpr uint32_t entry=0x100008;
  std::array<PS2Runtime::RecompiledFunction,1> first{BootEntry}, second{ModuleEntry};
  cave::ps2::NativeHost host;
  const auto resolve=+[](std::string_view path)->std::string_view {
    return path=="cdrom0:\\DEATH.ELF;1" ? "death.elf" : std::string_view{};
  };
  auto boot=std::make_unique<PS2Runtime>(PS2Runtime::RecompiledProgram{entry,first},host,resolve);
  auto death=std::make_unique<PS2Runtime>(PS2Runtime::RecompiledProgram{entry,second},host,resolve);
  Require(boot->lookupFunction(entry)==BootEntry && death->lookupFunction(entry)==ModuleEntry,
          "Disc programs with the same entry address shared a function table");
  Require(boot->replaceFunction(entry,OtherModuleEntry) && death->lookupFunction(entry)==ModuleEntry,
          "A function override leaked into another disc program");

  std::array<uint8_t,128> memory{};
  constexpr uint32_t pathAddress=4;
  std::memcpy(memory.data()+pathAddress,"cdrom0:\\DEATH.ELF;1",sizeof("cdrom0:\\DEATH.ELF;1"));
  R5900Context context{};
  SET_GPR_U32(&context,4,pathAddress);
  SET_GPR_U32(&context,5,1);
  boot->handleSyscall(memory.data(),&context,6);
  Require(Result(context)==-1 && !boot->isStopRequested() && boot->requestedExecutable().empty(),
          "LoadExecPS2 accepted unsupported guest arguments");
  SET_GPR_U32(&context,5,0);
  boot->handleSyscall(memory.data(),&context,6);
  Require(boot->isStopRequested() && boot->requestedExecutable()=="death.elf" && !death->isStopRequested(),
          "LoadExecPS2 did not stop only the requesting executable");
}
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
void EeServiceState() {
  auto liveMachine = std::make_unique<PS2Runtime>();
  auto& live = *liveMachine;
  live.configureGuestHeap(0x100000, 0x200000);
  const auto first = live.guestMalloc(2500);
  const auto aligned = live.guestMalloc(133, 2048);
  Require(first && aligned > first && !(aligned & 2047), "Heap fixture could not reserve aligned memory");
  live.guestFree(first);
  live.addEeExitHandler(7, 0x111000, 15);
  live.addEeExitHandler(7, 0x111004, 16);
  live.setEeSyscallOverride(nullptr, 43, 0x123450);
  const auto bytes = live.saveEeServices();
  auto coldMachine = std::make_unique<PS2Runtime>();
  auto& cold = *coldMachine;
  cold.loadEeServices(bytes);
  auto truncated = bytes;
  truncated.pop_back();
  bool rejected = false;
  try { cold.loadEeServices(truncated); } catch (const std::invalid_argument&) { rejected = true; }
  Require(rejected && cold.saveEeServices() == bytes, "Invalid EE services partially replaced the guest heap");
  const auto handlers = cold.takeEeExitHandlers(7);
  uint32_t target{};
  Require(handlers.size() == 2 && handlers[0].function == 0x111000 && handlers[1].argument == 16 &&
          cold.findEeSyscallOverride(43, target) && target == 0x123450, "Restored EE handlers changed");
  Require(live.guestMalloc(2400) == first && cold.guestMalloc(2400) == first,
          "Restored EE allocator lost its first free range");
  cold.guestFree(aligned);
  live.guestFree(aligned);
  Require(cold.guestMalloc(0x8000, 256) == live.guestMalloc(0x8000, 256),
          "Restored EE allocator failed to coalesce and reuse live space");
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
void DiscFileIo(const PS2Runtime& runtime) {
  const auto temporary=std::filesystem::temp_directory_path()/
      ("evac-disc-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  struct RemoveImage {
    std::filesystem::path path;
    ~RemoveImage() {std::error_code error;std::filesystem::remove(path,error);}
  } cleanup{temporary};
  const auto bytes=ps2_iso_fixture::Image();
  std::ofstream image(temporary,std::ios::binary);
  Require(bool(image.write(bytes.data(),bytes.size())),"Cannot create authored disc fixture");
  image.close();

  PS2Vfs files;
  files.mountDiscImage(temporary);
  auto unpacked=temporary;
  unpacked += ".unpacked";
  const PS2VfsMounts mounts{unpacked,unpacked,unpacked};
  const auto& rom=runtime.romDevice();
  constexpr auto path="cdrom0:\\DATA\\BANK.;1";
  const auto fd=files.open(path,PS2_FIO_O_RDONLY,mounts,rom);
  PS2VfsStat info;
  Require(fd>=3 && files.stat(path,mounts,rom,info) && info.size==8 && info.readOnly,
          "Missing extracted resource was not resolved in the mounted disc");
  std::array<char,16> buffer{};
  Require(files.seek(fd,-3,SEEK_END)==5 && files.read(fd,buffer.data(),buffer.size())==3 &&
          std::string_view(buffer.data(),3)=="re!" && files.read(fd,buffer.data(),1)==0,
          "Disc descriptor read past its extent or returned the wrong offset");
  Require(files.seek(fd,1,SEEK_CUR)==-1 && files.write(fd,buffer.data(),1)==-1 &&
          files.close(fd)==0 && files.open(path,PS2_FIO_O_RDWR,mounts,rom)==-1,
          "Disc descriptor did not enforce extent boundaries and read-only access");

  auto roots = mounts;
  roots.memoryCard0Root = temporary.parent_path();
  const std::string name = temporary.filename().string() + ".card";
  RemoveImage removeCard{roots.memoryCard0Root / name};
  const std::string cardPath = "mc0:" + name;
  const auto disc = files.open(path, PS2_FIO_O_RDONLY, roots, rom);
  const auto card = files.open(cardPath, PS2_FIO_O_RDWR | PS2_FIO_O_CREAT | PS2_FIO_O_EXCL | PS2_FIO_O_TRUNC, roots, rom);
  Require(disc > fd && card > disc && files.seek(disc, 5, SEEK_SET) == 5 &&
          files.write(card, "ABCDE", 5) == 5 && files.seek(card, 2, SEEK_SET) == 2,
          "Could not prepare EE descriptor snapshot");
  const auto state = files.saveState(roots);
  Require(files.close(disc) == 0 && files.close(card) == 0, "Could not close EE fixture descriptors");
  PS2Vfs cold;
  cold.mountDiscImage(temporary);
  cold.loadState(state, roots, rom);
  Require(cold.read(disc, buffer.data(), 8) == 3 && std::string_view(buffer.data(), 3) == "re!" &&
          cold.read(card, buffer.data(), 3) == 3 && std::string_view(buffer.data(), 3) == "CDE",
          "Cold EE file restore lost offset or reapplied create/truncate");
  PS2VfsMounts missing = roots;
  missing.memoryCard0Root = unpacked;
  bool rejected = false;
  try { cold.loadState(state, missing, rom); } catch (const std::runtime_error&) { rejected = true; }
  Require(rejected && cold.seek(card, 0, SEEK_CUR) == 5 && !std::filesystem::exists(unpacked / name),
          "Missing card file was recreated or damaged the running descriptor");
  const auto append = cold.open(cardPath, PS2_FIO_O_WRONLY | PS2_FIO_O_APPEND, roots, rom);
  Require(append == card + 1 && cold.seek(append, 1, SEEK_SET) == 1, "EE descriptor sequence changed");
  const auto appendState = cold.saveState(roots);
  cold.loadState(appendState, roots, rom);
  Require(cold.write(append, "F", 1) == 1 && cold.close(append) == 0 &&
          cold.seek(card, 0, SEEK_SET) == 0 && cold.read(card, buffer.data(), 8) == 6 &&
          std::string_view(buffer.data(), 6) == "ABCDEF", "EE descriptor lost append semantics after restore");
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

void EeSchedulerClockTest::CheckCallbackStacks() {
  auto runtime = std::make_unique<PS2Runtime>();
  auto& scheduler = runtime->eeScheduler();
  scheduler.reset(nullptr, {});
  scheduler.makeDormant(*scheduler.thread(1));
  const uint32_t first = runtime->asyncCallbackStackTop(0);
  Require(first && runtime->asyncCallbackStackTop(27) && !runtime->asyncCallbackStackTop(28),
          "Unexpected bounds for the low-memory callback stack arena");
  for (int n = 0; n < 64; ++n) {
    const int id = scheduler.createThread({});
    Require(id > 1 && scheduler.startThread(id, 0, {}, false) == 0, "Cannot start callback owner");
    auto* owner = scheduler.selectReady();
    Require(owner && owner->id == id, "Wrong callback owner");
    scheduler.makeRunning(*owner);
    try { scheduler.invokeCurrent({}); } catch (const EeDispatcherTransfer&) {}
    Require(getRegU32(&owner->activeContext(), 29) == first, "Finished thread retained its callback stack");
    try { scheduler.invokeCurrent({}); } catch (const EeDispatcherTransfer&) {}
    Require(getRegU32(&owner->activeContext(), 29) == runtime->asyncCallbackStackTop(1),
            "Nested callbacks must have separate live stacks");
    try { scheduler.exitCurrent(true); } catch (const EeDispatcherTransfer&) {}
  }

  constexpr uint32_t callback = 0x700000;
  constexpr uint32_t done = callback + 4, supplied = 0x203000;
  Require(runtime->registerFunction(callback, +[](uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const auto n = getRegU32(ctx, 4);
    Require(ram[0] == n && getRegU32(ctx, 2) == n &&
            getRegU32(ctx, 29) == ((n & 1) ? supplied : rt->asyncCallbackStackTop(0)),
            "Sequential handlers lost their order, previous result or assigned stack");
    ++ram[0];
    SET_GPR_U32(ctx, 2, n + 1);
    ctx->pc = 0;
  }), "Cannot register callback");
  Require(runtime->registerFunction(done, +[](uint8_t*, R5900Context* ctx, PS2Runtime* rt) {
    Require(getRegU32(ctx, 2) == 64, "Final handler result did not reach its parent");
    rt->eeScheduler().requestStop();
  }), "Cannot register final callback");
  std::vector<uint8_t> ram(PS2_RAM_SIZE);
  R5900Context main{};
  main.pc = done;
  scheduler.reset(ram.data(), main);
  scheduler.makeRunning(*scheduler.selectReady());
  std::vector<GuestInvocation> sequence(64);
  for (unsigned n = 0; n < sequence.size(); ++n) {
    auto& call = sequence[n];
    call.context.pc = callback;
    call.returnAction = EeReturnV0{};
    SET_GPR_U32(&call.context, 4, n);
    if (n & 1) SET_GPR_U32(&call.context, 29, supplied);
  }
  try { scheduler.invokeCurrentSequence(std::move(sequence)); } catch (const EeDispatcherTransfer&) {}
  const auto pending = scheduler.saveState();
  scheduler.reset(ram.data(), {});
  scheduler.loadState(pending);
  scheduler.run();
  Require(ram[0] == 64, "Restored callback sequence incomplete");
}

void EeSchedulerClockTest::CheckExternalPause(bool sleeping) {
  using namespace std::chrono_literals;
  constexpr uint32_t spin = 0x700000, complete = spin + 4;
  auto runtime = std::make_unique<PS2Runtime>();
  Require(runtime->memory().initialize() && runtime->syncCoreSubsystems(), "Cannot start pause fixture");
  auto& scheduler = runtime->eeScheduler();
  static std::atomic<bool> executed{}, completed{};
  executed = false;
  completed = false;
  Require(runtime->registerFunction(spin, +[](uint8_t*, R5900Context* ctx, PS2Runtime*) {
    SET_GPR_U32(ctx, 2, getRegU32(ctx, 2) + 1);
    executed = true;
  }), "Cannot register pause fixture");
  Require(runtime->registerFunction(complete, +[](uint8_t*, R5900Context*, PS2Runtime* rt) {
    completed = true;
    rt->eeScheduler().requestStop();
  }), "Cannot register pause completion");
  std::promise<void> ready;
  std::thread ee([&] {
    R5900Context context{};
    context.pc = spin;
    scheduler.reset(runtime->memory().getRDRAM(), context);
    if (sleeping) {
      scheduler.makeRunning(*scheduler.selectReady());
      try { scheduler.sleepCurrent(); } catch (const EeDispatcherTransfer&) {}
      scheduler.m_deadlines.clear();
      scheduler.updateNextDeadline();
    }
    ready.set_value();
    scheduler.run();
  });
  struct StopAndJoin {
    EeScheduler& scheduler;
    std::thread& ee;
    ~StopAndJoin() { scheduler.requestStop(); ee.join(); }
  } cleanup{scheduler, ee};
  ready.get_future().get();
  if (!sleeping) {
    const auto until = std::chrono::steady_clock::now() + 2s;
    while (!executed && std::chrono::steady_clock::now() < until) std::this_thread::yield();
    Require(executed, "Pause fixture did not execute the main thread");
  }
  scheduler.setPaused(true);
  auto run = [&](std::function<void()> job) {
    auto done = scheduler.whilePaused(std::move(job));
    Require(done.wait_for(2s) == std::future_status::ready, "EE dispatcher did not acknowledge the pause");
    done.get();
  };
  std::vector<uint8_t> saved;
  run([&] { saved = scheduler.saveState(); });
  // Different wall-clock instants while the menu is open must describe
  // exactly the same guest clocks and wait deadlines.
  std::this_thread::sleep_for(3ms);
  run([&] {
    Require(scheduler.saveState() == saved, "Paused guest or its deadlines advanced in the menu");
    scheduler.loadState(saved);
    Require(scheduler.saveState() == saved, "Restored menu snapshot changed its time origin");
    scheduler.thread(1)->activeContext().pc = complete;
    if (sleeping) scheduler.wakeupThread(1, false);
  });
  scheduler.setPaused(false);
  const auto until = std::chrono::steady_clock::now() + 2s;
  while (!completed && std::chrono::steady_clock::now() < until) std::this_thread::yield();
  Require(completed, "Resume failed to dispatch the restored thread");
}

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
  scheduler.setGsStatusNotifications(true);
  auto* running=scheduler.threadForDispatch(nullptr);
  Require(running && scheduler.threadForDispatch(running)==running,"Lost running thread between dispatches");
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
  running=scheduler.threadForDispatch(running);
  const auto shift=scheduler.m_hostClockOrigin-oldOrigin;
  auto wake=std::find_if(scheduler.m_deadlines.begin(),scheduler.m_deadlines.end(),
      [](const auto& entry){return entry.event.type==EeEventType::ExternalWake;});
  Require(shift>std::chrono::milliseconds(900) && wake!=scheduler.m_deadlines.end() &&
          wake->deadlineCycle==5*start && wake->hostDeadline-oldWake==shift,
          "A host stall must rebase all future deadlines without skipping guest time");
  Require(scheduler.currentVSyncTick()==1 && sink->frames.size()==(waitForFlip?0u:1u),"Wrong start-of-blank latch");
  if(flipDuringBlank)runtime->memory().gs().dispfb2=0x1446;
  running=scheduler.threadForDispatch(running);
  Require(sink->frames.size()==(!waitForFlip || flipDuringBlank?1u:0u),"Did not use the completed display flip");
  sink->busy=true;
  const auto end=std::min_element(scheduler.m_deadlines.begin(),scheduler.m_deadlines.end(),
      [](const auto& a,const auto& b){return a.deadlineCycle<b.deadlineCycle;});
  scheduler.m_eeCycle=end->deadlineCycle;
  running=scheduler.threadForDispatch(running);
  Require(scheduler.m_deferredVBlank.has_value()==(waitForFlip && !flipDuringBlank),"Busy field was lost or deferred twice");
  sink->busy=false;
  Require(scheduler.threadForDispatch(running)!=nullptr,"Lost running thread on scanout retry");
  Require(!scheduler.m_deferredVBlank && sink->frames.size()==1 && sink->frames[0].vsyncTick==1 &&
          sink->frames[0].field==1 && sink->frames[0].dispfb2==(waitForFlip && flipDuringBlank?0x1446u:0x1400u),
          "Wrong framebuffer, field parity or duplicate after scanout retry");
  Require(scheduler.m_deadlines.size()==2 && std::any_of(scheduler.m_deadlines.begin(),scheduler.m_deadlines.end(),
              [start](const auto& event){return event.event.type==EeEventType::VBlankStart && event.deadlineCycle==2*start;}),
          "Changing scanout shifted the next VBlank");
}
void EeSchedulerClockTest::CheckDiscWait(PS2Runtime& runtime) {
  const auto imagePath = std::filesystem::temp_directory_path() /
      ("evac-ee-stream-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".iso");
  struct Cleanup {
    PS2Runtime::IoPaths paths;
    std::filesystem::path image;
    ~Cleanup() {
      PS2Runtime::setIoPaths(paths);
      std::error_code ignored;
      std::filesystem::remove(image, ignored);
    }
  } cleanup{PS2Runtime::getIoPaths(), imagePath};
  auto image = ps2_iso_fixture::Image();
  image.replace(23 * 2048, 8, "part-two");
  std::ofstream out(imagePath, std::ios::binary);
  Require(bool(out.write(image.data(), image.size())), "Cannot write EE disc fixture");
  out.close();
  auto paths = cleanup.paths;
  paths.cdImage = imagePath;
  PS2Runtime::setIoPaths(paths);

  auto& scheduler = runtime.eeScheduler();
  std::vector<uint8_t> memory(PS2_RAM_SIZE);
  auto* ram = memory.data();
  R5900Context context{};
  context.pc = 0x100000;
  scheduler.reset(ram, context);
  scheduler.makeRunning(*scheduler.selectReady());
  auto& regs = *scheduler.currentContext();
  constexpr uint32_t data = 0x40000, error = 0x20000;
  SET_GPR_U32(&regs, 4, 1);
  SET_GPR_U32(&regs, 5, 1);
  SET_GPR_U32(&regs, 6, data);
  ps2_stubs::sceCdStInit(ram, &regs, &runtime);
  SET_GPR_U32(&regs, 4, 22);
  SET_GPR_U32(&regs, 5, 0);
  ps2_stubs::sceCdStStart(ram, &regs, &runtime);
  ++scheduler.m_vsyncTick;
  SET_GPR_U32(&regs, 4, 2);
  SET_GPR_U32(&regs, 5, data);
  SET_GPR_U32(&regs, 6, 1);
  SET_GPR_U32(&regs, 7, error);
  try { ps2_stubs::sceCdStRead(ram, &regs, &runtime); } catch (const EeDispatcherTransfer&) {}
  const auto* waiter = scheduler.thread(EeScheduler::kMainThreadId);
  Require(waiter && waiter->status == EeThreadStatus::Waiting &&
          std::get<EeCdStreamRead>(waiter->wait.completion).sectorsRead == 1 &&
          std::memcmp(ram + data, "fixture!", 8) == 0, "EE thread did not yield halfway through a disc read");
  const auto cd = ps2_stubs::saveCdState();
  const auto encoded = scheduler.saveState();
  context = waiter->context;
  const auto tick = scheduler.m_vsyncTick;
  ps2_stubs::sceCdStStop(ram, &context, &runtime);
  scheduler.reset(ram, R5900Context{});
  auto truncated = encoded;
  truncated.pop_back();
  bool rejected = false;
  try { scheduler.loadState(truncated); } catch (const std::invalid_argument&) { rejected = true; }
  Require(rejected && scheduler.thread(EeScheduler::kMainThreadId)->status == EeThreadStatus::Ready,
          "Incomplete EE state changed the live scheduler");
  scheduler.loadState(encoded);
  waiter = scheduler.thread(EeScheduler::kMainThreadId);
  Require(waiter && waiter->status == EeThreadStatus::Waiting && scheduler.m_vsyncTick == tick &&
          getRegU32(&waiter->context, 5) == data, "EE restore lost a waiting thread or its registers");
  const auto wake = std::get<EeVSyncWait>(waiter->wait.payload).afterTick + 1;
  ps2_stubs::loadCdState(cd);
  scheduler.m_vsyncTick = wake;
  scheduler.completeVSync(wake);
  auto* ready = scheduler.selectReady();
  Require(ready && ready->id == EeScheduler::kMainThreadId, "Restored EE disc wait did not wake");
  scheduler.makeRunning(*ready);
  const auto resume = std::exchange(ready->resumeCompletion, {});
  scheduler.resumeWait(resume, ready->activeContext());
  uint32_t readError{};
  std::memcpy(&readError, ram + error, sizeof(readError));
  Require(getRegU32(&ready->activeContext(), 2) == 2 && readError == 0 &&
          std::memcmp(ram + data, "fixture!", 8) == 0 && std::memcmp(ram + data + 2048, "part-two", 8) == 0,
          "Restored EE read did not finish the original request");
  scheduler.reset(nullptr, {});
  std::cout << "EE disc state: half-read request restored and guest thread resumed PASS\n";
}

int main(int argc,char** argv) {
  try {
    if(argc==5 && std::string_view(argv[1])=="--replay-dma") {
      ReplayPrivateDma(argv[2],argv[3],argv[4]);return 0;
    }
    if (argc == 1) {
      FloatToWord();
      VifDmaBurst();
    }
    PS2Runtime runtime;
    if(argc==2 && std::string_view(argv[1])=="--timer-bench")return EeTimerBench(runtime.memory());
    if(argc!=1)throw std::runtime_error("Unknown kernel test option");
    EeSchedulerClockTest::Run(runtime);
    EeSchedulerClockTest::CheckCallbackStacks();
    EeSchedulerClockTest::CheckExternalPause(false);
    EeSchedulerClockTest::CheckExternalPause(true);
    EeSchedulerClockTest::CheckDisplayFlip(false,true);
    EeSchedulerClockTest::CheckDisplayFlip(true,true);
    EeSchedulerClockTest::CheckDisplayFlip(true,false);
    DmaLongChain();
    VifTagPayloads();
    VifDmaWorkerOrdering();
    EeTimerArithmetic(runtime.memory());
    EeTimerDeferredEquivalence();
    ExplicitIoRoots(runtime);
    EeServiceState();
    DiscFileIo(runtime);
    EeSchedulerClockTest::CheckDiscWait(runtime);
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
    const auto callbackTop=runtime.asyncCallbackStackTop(0);
    Require(callbackTop>=0x90000+0x4000-16 && callbackTop<0x100000,
            "Callback stack aliases the game heap or main stack");
    Require(runtime.memory().initialize(),"Headless memory initialization failed");
    VuMicrocodeUpdates(runtime);
    VuPacketRestore(runtime);
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
    DiscExecutableSwitch();
    MemoryCardHandles(runtime);
    VifMixedBurst();
    VifPath1Burst();
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
