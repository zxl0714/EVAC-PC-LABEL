#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

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
void LoadOtherModule(uint8_t* memory,R5900Context* context,PS2Runtime*) {
  const uint32_t other=17;
  std::memcpy(memory+0x200004,&other,4);
  context->pc=getRegU32(context,31);
}
}

int main() {
  try {
    PS2Runtime runtime;
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
    Require(runtime.memory().initialize(),"Headless memory initialization failed");
    const std::array<PS2Runtime::RecompiledFunction,2> table{ModuleEntry,nullptr};
    runtime.registerStaticModule({16,0x200000,64,16,16,0x200040,table});
    Require(runtime.registerFunction(0x100000,BootEntry),"Boot function registration failed");
    Require(runtime.hasFunction(0x100000) && !runtime.hasFunction(0x200040),
            "Unloaded bank affected boot lookup");
    const std::array<uint32_t,6> moduleHeader{0x336f574d,16,0x200000,64,16,16};
    std::memcpy(runtime.memory().getRDRAM()+0x200000,moduleHeader.data(),sizeof(moduleHeader));
    Require(runtime.hasFunction(0x200040) && runtime.lookupFunction(0x200040)==ModuleEntry &&
            runtime.lookupFunction(0x100000)==BootEntry && !runtime.hasFunction(0x200044),
            "Runtime module dispatch lost selected bank, boot entry or sparse slot");
    const std::array<PS2Runtime::RecompiledFunction,2> otherTable{OtherModuleEntry,nullptr};
    runtime.registerStaticModule({17,0x200000,64,16,16,0x200040,otherTable});
    Require(runtime.registerFunction(0x100100,LoadOtherModule),"Loader fixture registration failed");
    SET_GPR_U32(&context,31,0x200048);
    Require(!runtime.dispatchGuestBranch(runtime.memory().getRDRAM(),&context,0x100100,
              0x200044,0x200048,PS2Runtime::GuestBranchKind::DirectCall,"JAL") &&
            context.pc==0x200048 && runtime.lookupFunction(0x200040)==OtherModuleEntry,
            "Native caller continued after its guest code bank was replaced");
    std::cout<<"PS2 kernel: EE ABI, priority inheritance, boundaries and restore passed\n";
    return 0;
  }catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
  }
}
