#include "static_modules.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void A(uint8_t*,R5900Context*,PS2Runtime*) {}
void B(uint8_t*,R5900Context*,PS2Runtime*) {}
void Require(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
template<class Callback> void Reject(Callback callback) {
  bool rejected=false;
  try {callback();}catch(const std::invalid_argument&) {rejected=true;}
  Require(rejected,"Invalid module was registered");
}
}

int main() {
  try {
    using namespace cave::ps2;
    std::array<uint8_t,512> memory{};
    const std::array<Function,3> first{A,nullptr,A},second{B,B,nullptr};
    const StaticModule a{16,64,32,16,32,128,first};
    const StaticModule b{17,64,32,8,32,128,second};
    StaticModules modules;
    modules.add(memory,a);modules.add(memory,b);
    Require(!modules.lookup(128),"Unloaded module was callable");
    auto activate=[&](uint32_t id,uint32_t dataSize) {
      const std::array<uint32_t,6> header{0x336f574d,id,64,32,dataSize,32};
      std::memcpy(memory.data()+64,header.data(),sizeof(header));
    };
    activate(16,16);
    Require(modules.lookup(128)==A && !modules.lookup(132) && modules.lookup(136)==A,
            "First bank or sparse entry lost");
    Require(modules.selected(132) && modules.selected(132)->id==16,
            "Caller identity at non-entry instruction lost");
    activate(17,8);
    Require(modules.lookup(128)==B && modules.lookup(132)==B && !modules.lookup(136),
            "Same-address bank did not switch");
    activate(16,8);
    Require(!modules.lookup(128),"Mismatched module layout was accepted");
    activate(18,16);
    Require(!modules.lookup(128),"Unknown module was callable");
    activate(16,16);
    Require(!modules.lookup(127) && !modules.lookup(129) && !modules.lookup(140),
            "Unaligned or out-of-table PC accepted");
    memory[64]=0;
    Require(!modules.lookup(128),"Invalid header signature accepted");
    Reject([&] {modules.add(memory,a);});
    auto invalid=a;invalid.tableAddress=156;
    Reject([&] {StaticModules{}.add(memory,invalid);});
    invalid=a;invalid.address=500;
    Reject([&] {StaticModules{}.add(memory,invalid);});
    invalid=a;invalid.textSize=0;
    Reject([&] {StaticModules{}.add(memory,invalid);});
    std::array<uint8_t,512> other{};
    invalid=a;invalid.id=19;
    Reject([&] {modules.add(other,invalid);});
    modules.clear();activate(16,16);
    Require(!modules.lookup(128),"Cleared module remained callable");
    StaticModules independent;
    independent.add(memory,a);
    Require(independent.lookup(128)==A && !modules.lookup(128),"Runtime state leaked");
    std::cout<<"PS2 modules: same-address banks, sparse lookup, headers and bounds passed\n";
    return 0;
  }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
