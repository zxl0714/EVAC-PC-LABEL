#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "register_functions.cpp"
#include <array>
#include <iostream>
#include <stdexcept>

int main() {
  try {
    std::array<uint8_t,2048> memory;memory.fill(0xa5);
    R5900Context context{};
    context.pc=0x100000;
    context.r[4]=_mm_cvtsi32_si128(16);
    context.r[5]=_mm_cvtsi32_si128(0x42);
    context.r[6]=_mm_cvtsi32_si128(8);
    context.r[29]=_mm_cvtsi32_si128(1024);
    FUN_00100000_0x100000(memory.data(),&context,nullptr);
    for(size_t index=8;index<32;++index) {
      const uint8_t expected=index>=16 && index<24?0x42:0xa5;
      if(memory[index]!=expected)throw std::runtime_error("Direct call bypassed its generated SDK binding");
    }
    if(context.pc!=0 || _mm_cvtsi128_si32(context.r[31])!=0)
      throw std::runtime_error("SDK direct-call return PC was lost");
    std::cout<<"PS2 generated direct SDK call and return passed\n";
    return 0;
  }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
