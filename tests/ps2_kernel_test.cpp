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
    std::cout<<"PS2 kernel: EE ABI, priority inheritance, boundaries and restore passed\n";
    return 0;
  }catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
  }
}
