#pragma once
#include "ps2_runtime.h"
#include "ps2_runtime_calls.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"

namespace cave::ps2 {
template<PS2Runtime::RecompiledFunction Handler>
void SdkEntry(uint8_t* memory,R5900Context* context,PS2Runtime* runtime) {
  // The recompiler's native stubs establish the guest return PC before calling
  // a service. Blocking services preserve it when they yield to the scheduler.
  context->pc=uint32_t(_mm_cvtsi128_si32(context->r[31]));
  Handler(memory,context,runtime);
}

inline PS2Runtime::RecompiledFunction SdkHandler(std::string_view name) {
  const auto syscall=ps2_runtime_calls::resolveSyscallName(name);
#define PS2_BIND_SYSCALL(symbol) if(syscall==#symbol)return &SdkEntry<&ps2_syscalls::symbol>;
  PS2_SYSCALL_LIST(PS2_BIND_SYSCALL)
#undef PS2_BIND_SYSCALL
  const auto stub=ps2_runtime_calls::resolveStubName(name);
#define PS2_BIND_STUB(symbol) if(stub==#symbol)return &SdkEntry<&ps2_stubs::symbol>;
  PS2_STUB_LIST(PS2_BIND_STUB)
#undef PS2_BIND_STUB
  return nullptr;
}

inline bool BindSdkHandler(PS2Runtime& runtime,uint32_t address,std::string_view name) {
  const auto handler=SdkHandler(name);
  return handler && runtime.replaceFunction(address,handler);
}
}
