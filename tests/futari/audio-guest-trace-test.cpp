// Deterministic simulated guest memory. Does not run a game or touch saves.
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/thread_state.h>
#include <windows.h>
#include <array>
#include <cassert>
#include <cstring>
#include <thread>
#include <chrono>
#include "audio_guest_trace.h"
extern "C" void FutariAudioGuestEvent(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
void FutariAudioGuestTestStart(uint8_t*);
uint64_t FutariAudioGuestTestDropped();
void FutariAudioEnqueue0Begin(PPCRegister&,PPCRegister&,PPCRegister&,PPCRegister&);
void FutariAudioEnqueue0End(PPCRegister&,PPCRegister&,PPCRegister&,PPCRegister&);
void FutariAudioPowerActionBegin(PPCRegister&,PPCRegister&,PPCRegister&,PPCRegister&);
void FutariAudioPowerActionEnd(PPCRegister&,PPCRegister&,PPCRegister&,PPCRegister&);
void FutariAudioDeathPhase(PPCRegister&,PPCRegister&,PPCRegister&,PPCRegister&);
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  rex::InitLogging(argv[1]);
  auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x100000000ULL,MEM_RESERVE,PAGE_NOACCESS));
  if(!base)return 3;
  for(uint32_t start:{0x88000000U,0x884D0000U,0x885B0000U}) {
    if(!VirtualAlloc(base+start,0x20000,MEM_COMMIT,PAGE_READWRITE))return 4;
  }
  auto write=[&](uint32_t p,uint32_t n){n=_byteswap_ulong(n);std::memcpy(base+p,&n,4);};
  auto read=[&](uint32_t p){uint32_t n;std::memcpy(&n,base+p,4);return _byteswap_ulong(n);};
  FutariAudioGuestTestStart(base);
  write(0x8800A0BC,64);std::memcpy(base+0x88009B48,"se01a",6);
  FutariAudioGuestEvent(60,0x8800A0C0,1,0x88009B48,0);
  PPCRegister ra{},rb{},rc{},rd{};ra.u64=5;rb.u64=64;rc.u64=70;rd.u64=37;
  // Safe actual TLS-context lookup, not a guessed host stack return address.
  rex::runtime::ThreadState test_thread(77,0,0,nullptr);
  test_thread.context()->lr=0xABCDEF0088123458ULL;
  std::array<uint8_t,sizeof(PPCContext)> context_before{};
  std::memcpy(context_before.data(),test_thread.context(),context_before.size());
  rex::runtime::ThreadState::Bind(&test_thread);
  FutariAudioEnqueue0Begin(ra,rb,rc,rd);
  const std::array<uint8_t,5> command={64,0,70,37,5};
  std::memcpy(base+0x884E10D0,command.data(),5);write(0x884DFDF4,1);
  FutariAudioEnqueue0End(ra,rb,rc,rd);
  if(std::memcmp(context_before.data(),test_thread.context(),context_before.size()))return 8;
  rex::runtime::ThreadState::Bind(nullptr);
  if(ra.u64!=5||rb.u64!=64||rc.u64!=70||rd.u64!=37)return 7;
  std::this_thread::sleep_for(std::chrono::milliseconds(280));
  FutariAudioGuestEvent(30,0,0,0,0);
  FutariAudioGuestEvent(40,1,64,70,0);
  FutariAudioGuestEvent(42,0x12345678,0,0,0x820E1234);
  write(0x884D0050,0x12345678);
  FutariAudioGuestEvent(41,0,0x884D0000,0x885CBB80,0);
  write(0x884E010C,1);FutariAudioGuestEvent(31,0,0,0,0);
  FutariAudioGuestEvent(43,0,0,0,0);
  write(0x884E1354,1);FutariAudioGuestEvent(30,0,0,0,0);write(0x884E1354,0);
  // A type-4 head wait is sampled, not emitted on every repeated invocation.
  FutariAudioGuestEvent(10,1,0,0,0);
  const std::array<uint8_t,5> wait_command={0,4,1,74,4};
  std::memcpy(base+0x884E10D5,wait_command.data(),5);write(0x884DFDF4,2);
  FutariAudioGuestEvent(20,1,0,0,0);
  write(0x885B7944,1);write(0x885B794C,74);write(0x885CBB80,0x77);
  for(int i=0;i<1000;++i){FutariAudioGuestEvent(30,0,0,0,0);FutariAudioGuestEvent(32,1,74,0,0);}
  write(0x884E010C,2);FutariAudioGuestEvent(31,0,0,0,0);
  // Ring index wrap still correlates the right command.
  write(0x884E010C,127);write(0x884DFDF4,127);
  FutariAudioGuestEvent(14,5,64,70,37);
  std::memcpy(base+0x884E10D0+127*5,command.data(),5);write(0x884DFDF4,0);
  FutariAudioGuestEvent(24,1,64,70,37);
  FutariAudioGuestEvent(30,0,0,0,0);write(0x884E010C,0);FutariAudioGuestEvent(31,0,0,0,0);
  // The test, not the logger, simulates the original actor-state update.
  constexpr uint32_t object=0x884D1000,actor=0x884D1100,data=0x884D1300;
  write(object,0x40);write(object+4,0);write(actor+80,data);write(actor+56,3);
  base[actor+158]=2;base[actor+84]=0;base[data+7]=3;
  ra.u64=object;rb.u64=actor;rc.u64=0x123456789;rd.u64=0xABCDEF012;
  std::array<uint8_t,0x308> actor_before{};
  std::memcpy(actor_before.data(),base+object,actor_before.size());
  FutariAudioPowerActionBegin(ra,rb,rc,rd);
  if(std::memcmp(actor_before.data(),base+object,actor_before.size()))return 9;
  write(actor+56,4);base[data+7]=4;
  std::memcpy(actor_before.data(),base+object,actor_before.size());
  FutariAudioPowerActionEnd(ra,rb,rc,rd);
  if(std::memcmp(actor_before.data(),base+object,actor_before.size()))return 10;
  if(ra.u64!=object||rb.u64!=actor||rc.u64!=0x123456789||rd.u64!=0xABCDEF012)return 11;
  base[actor+84]=3;ra.u64=actor;
  std::memcpy(actor_before.data(),base+object,actor_before.size());
  FutariAudioDeathPhase(ra,rb,rc,rd);
  if(std::memcmp(actor_before.data(),base+object,actor_before.size()))return 12;
  // Invalid/uncommitted pointers produce unavailable captures, not exceptions.
  FutariAudioGuestEvent(80,0,0xFFFFFFF0U,0,0);
  FutariAudioGuestEvent(81,0,0xFFFFFFF0U,0,0);
  FutariAudioGuestEvent(82,0x44400000U,0,0,0);
  FutariAudioGuestEvent(50,0,0,0,0);FutariAudioGuestEvent(51,0,0,0,0);
  FutariStopAudioGuestTrace();
  if(FutariAudioGuestTestDropped())return 5;
  // Disabled sink must not dereference any guest memory (base is now null).
  FutariAudioGuestEvent(30,0,0,0,0);
  FutariAudioGuestEvent(80,object,actor,0,0);
  FutariAudioGuestEvent(82,actor,0,0,0);
  if(read(0x884E010C)!=0||read(0x884DFDF4)!=0||std::memcmp(base+0x884E10D0,command.data(),5))return 6;
  VirtualFree(base,0,MEM_RELEASE);
  rex::ShutdownLogging();return 0;
}
