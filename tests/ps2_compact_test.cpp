#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

struct FixtureEntry { uint32_t pc; PS2Runtime::RecompiledFunction function; };
#include "fixture.h"

extern const uint32_t g_ps2RecompiledFunctionTableBase = 0x100000u;
extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0x100200u;
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = 128u;
PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[128]{};

namespace {
void YieldingLeaf(uint8_t*,R5900Context* context,PS2Runtime*) {
  SET_GPR_U32(context,2,getRegU32(context,2)+5);
  context->pc=0x100104;
}
template<size_t N>
R5900Context Run(const FixtureEntry(&entries)[N],uint32_t pc,unsigned seed,bool yield) {
  std::fill(std::begin(g_ps2RecompiledFunctionTable),std::end(g_ps2RecompiledFunctionTable),nullptr);
  for(const auto& entry:entries)g_ps2RecompiledFunctionTable[(entry.pc-0x100000)/4]=entry.function;
  if(yield)g_ps2RecompiledFunctionTable[0x100/4]=YieldingLeaf;
  PS2Runtime runtime;
  R5900Context context{};
  for(unsigned i=1;i<32;++i)context.r[i]=_mm_set_epi32(seed+i,seed-i,seed^i,seed+i*7);
  SET_GPR_U32(&context,4,1+seed%9);SET_GPR_U32(&context,5,seed%2);
  SET_GPR_U32(&context,8,0);SET_GPR_U32(&context,31,0);context.pc=pc;
  std::array<uint8_t,256> memory{};
  for(unsigned step=0;context.pc && step<100;++step) {
    if(context.pc<0x100000 || context.pc>=0x100200 || (context.pc&3))
      throw std::runtime_error("Compaction produced an invalid continuation");
    auto function=g_ps2RecompiledFunctionTable[(context.pc-0x100000)/4];
    if(!function)throw std::runtime_error("Compaction lost a callable continuation");
    function(memory.data(),&context,&runtime);
  }
  if(context.pc)throw std::runtime_error("Compacted fixture did not return");
  return context;
}
}

int main() {
  try {
    unsigned comparisons=0;
    for(uint32_t pc:{0x100000u,0x10000cu,0x100010u,0x100024u,0x100040u,0x100044u,
                    0x100080u,0x100094u,0x100100u,0x100104u})
      for(unsigned seed=0;seed<32;++seed)for(bool yield:{false,true}) {
        const auto expected=Run(baseline::entries,pc,seed,yield);
        const auto actual=Run(compact::entries,pc,seed,yield);
        if(std::memcmp(expected.r,actual.r,sizeof(expected.r)) || expected.pc!=actual.pc)
          throw std::runtime_error("Shared PS2 body changed architectural registers");
        ++comparisons;
      }
    std::cout<<"PS2 original/compact generated execution: "<<comparisons
             <<" comparisons passed (entries, direct/indirect calls, yields, likely branches, delay slots and loops)\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
