#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/timed_wait_policy.h>
#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <thread>
#include "audio_wait_policy.h"
void FutariAudioWaitBegin(PPCRegister&,PPCRegister&,PPCRegister&,PPCRegister&);
void FutariAudioWaitEnd(PPCRegister&,PPCRegister&,PPCRegister&,PPCRegister&);
int main(int argc,char** argv){
  if(argc!=2)return 2;
  const bool enabled=std::strcmp(argv[1],"on")==0;
  if(!enabled&&std::strcmp(argv[1],"off"))return 2;
  _putenv_s("FUTARI_AUDIO_WAIT_FIX",enabled?"1":"0");
  rex::InitLogging(nullptr,spdlog::level::off);
  SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_NORMAL);
  const int original=GetThreadPriority(GetCurrentThread());
  FutariStartAudioWaitPolicy();
  PPCRegister a{},b{},c{},d{};
  a.u64=0x1122334455667788ULL;b.u64=1;c.u64=0x123456789abcdef0ULL;d.u64=0xfedcba9876543210ULL;
  const auto aa=a.u64,bb=b.u64,cc=c.u64,dd=d.u64;
  // Zero-time poll does not activate the policy.
  b.u64=0;FutariAudioWaitBegin(a,b,c,d);FutariAudioWaitEnd(a,b,c,d);
  if(rex::system::ExchangeTimedWaitYieldSuppression(false)||GetThreadPriority(GetCurrentThread())!=original)return 3;
  b.u64=bb;
  FutariAudioWaitBegin(a,b,c,d);
  const bool previous=rex::system::ExchangeTimedWaitYieldSuppression(false);
  rex::system::ExchangeTimedWaitYieldSuppression(previous);
  if(previous!=enabled)return 4;
  if(GetThreadPriority(GetCurrentThread())!=(enabled?THREAD_PRIORITY_ABOVE_NORMAL:original))return 5;
  bool other_policy=true;int other_priority=-999;
  std::thread other([&]{other_policy=rex::system::ExchangeTimedWaitYieldSuppression(false);other_priority=GetThreadPriority(GetCurrentThread());});other.join();
  if(other_policy||other_priority!=THREAD_PRIORITY_NORMAL)return 6;
  FutariAudioWaitEnd(a,b,c,d);
  if(rex::system::ExchangeTimedWaitYieldSuppression(false))return 7;
  if(a.u64!=aa||b.u64!=bb||c.u64!=cc||d.u64!=dd)return 8;
  // Restore a pre-existing policy rather than resetting it blindly.
  rex::system::ExchangeTimedWaitYieldSuppression(true);
  FutariAudioWaitBegin(a,b,c,d);FutariAudioWaitEnd(a,b,c,d);
  if(!rex::system::ExchangeTimedWaitYieldSuppression(false))return 9;
  FutariStopAudioWaitPolicy();FutariLeaveAudioTimedWait();
  if(GetThreadPriority(GetCurrentThread())!=original)return 10;
  rex::ShutdownLogging();std::printf("AUDIO_WAIT_POLICY_%s_PASS: PPC registers unchanged, zero polls, TLS isolation, previous policy, priority restoration\n",argv[1]);return 0;
}
