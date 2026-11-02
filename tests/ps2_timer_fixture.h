#pragma once
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>

inline void EeTimerDeferredEquivalence() {
  // Force the reference to materialize after every advance. The candidate
  // retains many advances between reads, writes and observable interrupts.
  auto reference=std::make_unique<PS2Memory>();
  auto deferred=std::make_unique<PS2Memory>();
  auto observe=[&](PS2Memory& memory) {
    std::array<uint32_t,16> registers{};
    for(unsigned timer=0;timer<4;++timer)for(unsigned reg=0;reg<4;++reg)
      registers[timer*4+reg]=memory.readIORegister(0x10000000u+timer*0x800u+reg*0x10u);
    return registers;
  };
  uint32_t random=0x921733a5u;
  auto next=[&] { random=random*1664525u+1013904223u;return random; };
  for(unsigned timer=0;timer<4;++timer) {
    for(auto* memory:{reference.get(),deferred.get()}) {
      memory->writeIORegister(0x10000020u+timer*0x800u,127u+timer*239u);
      memory->writeIORegister(0x10000010u+timer*0x800u,0x380u|timer);
    }
  }
  for(unsigned step=0;step<100000;++step) {
    if(step%31==0) {
      const unsigned timer=next()%4,reg=next()%4;
      const auto address=0x10000000u+timer*0x800u+reg*0x10u;
      const auto value=next();
      reference->writeIORegister(address,value);
      deferred->writeIORegister(address,value);
    }
    const uint64_t cycles=step%4096==4095?UINT64_MAX:
        (step%503==0?294912000ull+(next()&65535u):(next()&127u));
    const auto referenceIrqs=reference->advanceEeTimers(cycles);
    const auto deferredIrqs=deferred->advanceEeTimers(cycles);
    observe(*reference);
    if(referenceIrqs!=deferredIrqs ||
       reference->cyclesUntilNextEeTimerInterrupt()!=deferred->cyclesUntilNextEeTimerInterrupt())
      throw std::runtime_error("Deferred EE timer changed an IRQ or next deadline");
    if(step%127==0 && observe(*reference)!=observe(*deferred))
      throw std::runtime_error("Deferred EE timer changed an observable register");
    if(step%8191==8190) {
      reference->resetEeTimers();deferred->resetEeTimers();
    }
  }
  if(observe(*reference)!=observe(*deferred))
    throw std::runtime_error("Deferred EE timer lost its final clock phase");
}

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
      const uint64_t cycles=step%19==0?0:
          (step%257==0?3*frequency+(random&8191u):1+(random&8191u));
      total+=cycles;interrupts|=memory.advanceEeTimers(cycles);
      const auto ticks=(total/frequency)*clocks[mode]+(total%frequency)*clocks[mode]/frequency;
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
  for(unsigned mode=0;mode<4;++mode) {
    for(const uint64_t phase:{1ull,17ull,513ull}) {
      memory.resetEeTimers();
      memory.writeIORegister(0x10000020,1234);
      memory.writeIORegister(0x10000010,mode|0x380u);
      if(memory.advanceEeTimers(phase))throw std::runtime_error("Timer fired before test phase");
      const auto deadline=memory.cyclesUntilNextEeTimerInterrupt();
      if(deadline<2 || deadline==UINT64_MAX || memory.advanceEeTimers(deadline-1))
        throw std::runtime_error("EE timer deadline precedes its interrupt");
      if(memory.cyclesUntilNextEeTimerInterrupt()!=1 || memory.advanceEeTimers(1)!=1)
        throw std::runtime_error("EE timer fractional deadline missed its interrupt");
    }
    memory.resetEeTimers();
    memory.writeIORegister(0x10000010,mode|0x80u);
    memory.advanceEeTimers(UINT64_MAX);
    const auto ticks=(UINT64_MAX/frequency)*clocks[mode]+
        (UINT64_MAX%frequency)*clocks[mode]/frequency;
    if(memory.readIORegister(0x10000000)!=(ticks&65535u))
      throw std::runtime_error("EE timer large cycle advance overflowed");
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
