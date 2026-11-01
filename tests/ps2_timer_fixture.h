#pragma once
#include <chrono>
#include <cstdint>
#include <iostream>

inline void EeTimerArithmetic(PS2Memory& memory) {
  // FIFO occupancy expires at the next nonzero EE advance, including a new
  // occupancy update after any number of idle checkpoints.
  constexpr uint32_t gifStat=0x10003020u,fqc=0x1f000000u;
  for(unsigned repeat=0;repeat<2;++repeat) {
    memory.writeIORegister(gifStat,0x10000100u);
    memory.advanceEeTimers(0);
    if((memory.readIORegister(gifStat)&fqc)!=0x10000000u)
      throw std::runtime_error("GIF occupancy expired without guest cycles");
    memory.advanceEeTimers(1);
    if(memory.readIORegister(gifStat)&fqc)
      throw std::runtime_error("GIF occupancy survived the next EE advance");
    if(!(memory.readIORegister(gifStat)&0x100u))
      throw std::runtime_error("GIF occupancy expiration changed unrelated status");
    for(unsigned i=0;i<32;++i)memory.advanceEeTimers(1);
  }
  memory.writeIORegister(gifStat,0);
  constexpr uint64_t frequency=294912000;
  constexpr uint64_t clocks[]{147456000,9216000,576000,15734};
  for(unsigned mode=0;mode<4;++mode)for(bool resetAtCompare:{false,true}) {
    memory.resetEeTimers();
    const unsigned compare=resetAtCompare?1234:54321;
    memory.writeIORegister(0x10000020,compare);
    memory.writeIORegister(0x10000010,mode|0x380u|(resetAtCompare?0x40u:0u));
    uint64_t total=0;uint32_t random=0x78123456,interrupts=0;
    for(unsigned step=0;step<10000;++step) {
      random=random*1664525u+1013904223u;
      const uint64_t cycles=step%19==0?0:1+(random&8191u);
      total+=cycles;interrupts|=memory.advanceEeTimers(cycles);
      const auto ticks=(total*clocks[mode])/frequency;
      const auto expected=resetAtCompare?ticks%compare:ticks%65536u;
      if(memory.readIORegister(0x10000000)!=expected)throw std::runtime_error("EE timer lost fractional cycle carry");
      const auto status=memory.readIORegister(0x10000010);
      if(bool(status&0x400u)!=(ticks>=compare) || bool(status&0x800u)!=(!resetAtCompare && ticks>=65536u) ||
         bool(interrupts&1u)!=(ticks>=compare || (!resetAtCompare && ticks>=65536u)))
        throw std::runtime_error("EE timer compare/overflow deadline changed");
    }
    // A clock switch resets the old fractional remainder at the MMIO boundary.
    memory.writeIORegister(0x10000000,0);
    memory.writeIORegister(0x10000010,((mode+1)%4)|0xf80u);
    memory.advanceEeTimers(1);
    if(memory.readIORegister(0x10000000)!=0)throw std::runtime_error("EE timer mode change retained an old clock phase");
  }
  memory.resetEeTimers();
}

inline int EeTimerBench(PS2Memory& memory) {
  constexpr unsigned iterations=10000000;
  for(unsigned i=0;i<4;++i) {
    memory.writeIORegister(0x10000000+i*0x800,0);
    memory.writeIORegister(0x10000020+i*0x800,54321);
    memory.writeIORegister(0x10000010+i*0x800,0x380u+i);
  }
  const auto start=std::chrono::steady_clock::now();uint32_t checksum=0;
  for(unsigned i=0;i<iterations;++i)checksum|=memory.advanceEeTimers(16+(i&63));
  const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
  for(unsigned i=0;i<4;++i)checksum+=memory.readIORegister(0x10000000+i*0x800);
  std::cout<<"{\"benchmark\":\"ee-four-timer-advance\",\"iterations\":"<<iterations
           <<",\"ns_per_call\":"<<double(ns)/iterations<<",\"checksum\":"<<checksum<<"}\n";
  return 0;
}
