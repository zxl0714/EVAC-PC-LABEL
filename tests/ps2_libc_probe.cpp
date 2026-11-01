// Runs the original ESPGaluda library translations without a window or disc.
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "ps2_recompiled_functions.h"
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

extern const uint32_t g_ps2RecompiledFunctionTableBase=0x118048;
extern const uint32_t g_ps2RecompiledFunctionTableEnd=0x11a138;
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount=
  (g_ps2RecompiledFunctionTableEnd-g_ps2RecompiledFunctionTableBase)/4;
PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[g_ps2RecompiledFunctionTableSlotCount]{};

namespace {
struct Library {const char* name;uint32_t begin,end;PS2Runtime::RecompiledFunction function;};
const Library libraries[]{
  {"memcpy",0x118048,0x1180f8,FUN_00118048_0x118048},
  {"strcat",0x119c70,0x119d9c,FUN_00119c70_0x119c70},
  {"strcpy",0x119ee8,0x119ffc,FUN_00119ee8_0x119ee8},
  {"strlen",0x11a000,0x11a138,FUN_0011a000_0x11a000}
};
void Execute(uint8_t* memory,R5900Context& context,PS2Runtime& runtime) {
  for(unsigned resumes=0;context.pc && resumes<4096;++resumes) {
    const Library* owner=nullptr;
    for(const auto& library:libraries)
      if(context.pc>=library.begin && context.pc<library.end)owner=&library;
    if(!owner) {
      std::ostringstream error;error<<"Unexpected library PC 0x"<<std::hex<<context.pc;
      throw std::runtime_error(error.str());
    }
    owner->function(memory,&context,&runtime);
  }
  if(context.pc)throw std::runtime_error("Library did not return within the resume limit");
}
}

int main() {
  try {
    PS2Runtime runtime;
    if(!runtime.memory().initialize())throw std::runtime_error("Cannot initialize test RAM");
    auto* memory=runtime.memory().getRDRAM();
    for(const auto& library:libraries)
      g_ps2RecompiledFunctionTable[(library.begin-g_ps2RecompiledFunctionTableBase)/4]=library.function;
    constexpr uint32_t Stack=0x1ee9f90,Source=0x400000,Destination=0x410000;
    constexpr std::array<unsigned,27> lengths{0,1,2,3,4,5,7,8,9,15,16,17,23,24,31,32,33,63,64,65,95,127,128,129,255,256,257};
    uint64_t cases{};
    const auto start=std::chrono::steady_clock::now();
    for(const auto& library:libraries)for(unsigned sourceAlignment=0;sourceAlignment<16;++sourceAlignment)
    for(unsigned destAlignment=0;destAlignment<16;++destAlignment)for(unsigned length:lengths) {
      const uint32_t source=Source+sourceAlignment,destination=Destination+destAlignment;
      std::memset(memory+Source-32,0xa5,1024);
      std::memset(memory+Destination-32,0xa5,1024);
      std::memset(memory+Stack-256,0xa5,512);
      for(unsigned i=0;i<length;++i)memory[source+i]=uint8_t(1+(i*37)%255);
      memory[source+length]=0;
      const bool concatenate=std::strcmp(library.name,"strcat")==0;
      const bool measure=std::strcmp(library.name,"strlen")==0;
      const bool bytes=std::strcmp(library.name,"memcpy")==0;
      const unsigned prefix=concatenate?3:0;
      if(concatenate)std::memcpy(memory+destination,"abc",4);
      R5900Context context{};
      context.pc=library.begin;
      SET_GPR_U32(&context,4,measure?source:destination);
      SET_GPR_U32(&context,5,source);SET_GPR_U32(&context,6,length);
      SET_GPR_U32(&context,29,Stack);
      std::array<__m128i,8> saved;
      for(unsigned i=0;i<saved.size();++i) {
        saved[i]=_mm_set_epi64x(0x12345678+i,0x23456789+i);
        context.r[16+i]=saved[i];
      }
      runtime.eeScheduler().reset(memory,context);
      Execute(memory,context,runtime);
      const unsigned written=length+(bytes?0:1);
      bool correct=getRegU32(&context,2)==(measure?length:destination) && getRegU32(&context,29)==Stack;
      for(unsigned i=0;i<saved.size();++i)
        correct &= _mm_movemask_epi8(_mm_cmpeq_epi8(context.r[16+i],saved[i]))==0xffff;
      if(!measure) {
        correct &= std::memcmp(memory+destination+prefix,memory+source,written)==0;
        correct &= memory[destination-1]==0xa5 && memory[destination+prefix+written]==0xa5;
        if(concatenate)correct &= std::memcmp(memory+destination,"abc",3)==0;
      }
      if(!correct) {
        std::ostringstream error;error<<library.name<<" failed: srcAlign="<<sourceAlignment
          <<" dstAlign="<<destAlignment<<" length="<<length<<" v0=0x"<<std::hex<<getRegU32(&context,2)
          <<" sp=0x"<<getRegU32(&context,29)<<" ra=0x"<<getRegU32(&context,31);
        throw std::runtime_error(error.str());
      }
      ++cases;
    }
    const double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"Original library: "<<cases<<" alignment, length, guard and saved-register cases passed in "
             <<milliseconds<<" ms\n";
    return 0;
  }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
