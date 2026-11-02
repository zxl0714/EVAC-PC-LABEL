#include "static_modules.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <string_view>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

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
void Bench() {
  using namespace cave::ps2;
  std::vector<uint8_t> ram(4u*1024u*1024u);
  const std::array<Function,8> table{A,A,A,A,A,A,A,A};
  StaticModules modules;
  for(uint32_t id=1;id<=30;++id)
    modules.add(ram,{id,0x60000,0x1000,0,0,0x60040,table});
  modules.add(ram,{31,0x200000,0x1000,0,0,0x200040,table});
  const std::array<uint32_t,6> header{0x336f574d,1,0x60000,0x1000,0,0};
  std::memcpy(ram.data()+0x60000,header.data(),sizeof(header));
  constexpr unsigned calls=10000000;
  for(unsigned mode=0;mode<4;++mode) {
    uint64_t checksum=0;
#ifdef _WIN32
    ULONG64 before{},after{};
    Require(QueryThreadCycleTime(GetCurrentThread(),&before),"Cannot read benchmark cycles");
#endif
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<calls;++i) {
      uint32_t pc=0x10000+(i&7u)*4;
      if(mode==1 || (mode==2 && i%4u==0))pc=0x60040+(i&7u)*4;
      if(mode==3)pc=0x300000+(i&7u)*4;
      checksum+=modules.lookup(pc)!=nullptr;
    }
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
#ifdef _WIN32
    Require(QueryThreadCycleTime(GetCurrentThread(),&after),"Cannot read benchmark cycles");
    const auto cycles=after-before;
#else
    const uint64_t cycles=0;
#endif
    const uint64_t expected=mode==1?calls:mode==2?calls/4u:0u;
    Require(checksum==expected,"Module lookup benchmark changed dispatch results");
    std::cout<<"{\"mode\":"<<mode<<",\"calls\":"<<calls<<",\"wall_ms\":"<<ms
      <<",\"cpu_cycles\":"<<cycles<<",\"checksum\":"<<checksum<<"}\n";
  }
}
}

int main(int argc,char** argv) {
  try {
    if(argc==2 && std::string_view(argv[1])=="--bench"){Bench();return 0;}
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
    // An earlier failed range lookup must not hide a later lower/higher bank.
    StaticModules ranges;
    auto upper=a;upper.id=20;upper.address=256;upper.tableAddress=320;
    ranges.add(memory,upper);
    Require(!ranges.lookup(128),"Unregistered lower range was callable");
    ranges.add(memory,a);
    activate(16,16);
    Require(ranges.lookup(128)==A,"New lower module was hidden by old bounds");
    const std::array<uint32_t,6> upperHeader{0x336f574d,20,256,32,16,32};
    std::memcpy(memory.data()+256,upperHeader.data(),sizeof(upperHeader));
    Require(ranges.lookup(320)==A,"Upper bank was hidden by new bounds");
    ranges.clear();ranges.add(memory,a);
    Require(!ranges.lookup(320) && ranges.lookup(128)==A,"Clear retained another module range");
    std::cout<<"PS2 modules: same-address banks, sparse lookup, headers and bounds passed\n";
    return 0;
  }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
