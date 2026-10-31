#include "ps2_vu_fixture.h"
#include <array>
#include <iostream>
#include <stdexcept>

struct Context {
  uint16_t vi[16]{};
  uint32_t vu0_cmsar0{},vu0_pc{};
};
struct Runtime {
  uint32_t address{};
  unsigned calls{};
  void vu0StartMicroProgram(uint8_t*,Context*,uint32_t value){address=value;++calls;}
};
int main() {
  try {
    for(const uint32_t cmsar:{0u,1u,511u,512u,0xffffu,0xffffffffu}) {
      Context context;context.vu0_cmsar0=cmsar;
      for(auto& value:context.vi)value=0x125;
      Runtime runtime;GeneratedVcallmsr(nullptr,&context,&runtime);
      const auto expected=(cmsar&511u)*8u;
      if(runtime.calls!=1 || runtime.address!=expected || context.vu0_pc!=expected)
        throw std::runtime_error("VCALLMSR did not use CMSAR0 with VU0 address wrapping");
    }
    std::cout<<"PS2 VCALLMSR: emitted C++ executed six register/wrap cases\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
