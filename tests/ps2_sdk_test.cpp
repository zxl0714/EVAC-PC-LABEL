#include "backends/ps2/sdk_binding.h"
#include <iostream>
#include <stdexcept>

namespace {
struct Yield {};
void Wait(uint8_t*,R5900Context* context,PS2Runtime*) {
  if(context->pc!=0x00123458)throw std::runtime_error("Blocking SDK call has no return PC");
  throw Yield{};
}
void Return(uint8_t*,R5900Context* context,PS2Runtime*) {
  context->r[2]=_mm_set_epi32(0,0,0,42);
}
void Transfer(uint8_t*,R5900Context* context,PS2Runtime*) {context->pc=0x00234568;}
}
int main() {
  try {
    R5900Context context;context.pc=0x00100000;
    context.r[31]=_mm_set_epi32(0,0,0,0x00123458);
    bool yielded=false;
    try {cave::ps2::SdkEntry<&Wait>(nullptr,&context,nullptr);}catch(const Yield&){yielded=true;}
    if(!yielded || context.pc!=0x00123458)throw std::runtime_error("SDK yield would re-enter the service");
    cave::ps2::SdkEntry<&Return>(nullptr,&context,nullptr);
    if(context.pc!=0x00123458 || _mm_cvtsi128_si32(context.r[2])!=42)
      throw std::runtime_error("SDK return registers were lost");
    cave::ps2::SdkEntry<&Transfer>(nullptr,&context,nullptr);
    if(context.pc!=0x00234568)throw std::runtime_error("SDK transfer was overwritten by a return");
    std::cout<<"PS2 SDK: yield continuation, normal return and explicit transfer passed\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
