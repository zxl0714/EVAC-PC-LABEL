// Host scheduling compatibility, scoped to the original sound worker's 1ms wait.
// No guest instruction, queue, cue, sample, timeout value or return is modified.
#include "audio_wait_policy.h"
#if defined(FUTARI_SCOPED_AUDIO_WAIT_FIX)
#include <rex/system/timed_wait_policy.h>
#include <rex/logging.h>
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace {
std::atomic<bool> active{false};
bool period_active=false;
struct WorkerScope {
  bool in_wait=false,previous_yield=false,priority_changed=false,reported=false,observed=false;
  int previous_priority=THREAD_PRIORITY_NORMAL;
  void RestorePriority() {
    if(priority_changed){SetThreadPriority(GetCurrentThread(),previous_priority);priority_changed=false;}
  }
  ~WorkerScope(){RestorePriority();}
};
thread_local WorkerScope worker;
}
void FutariStartAudioWaitPolicy() {
  const char* flag=std::getenv("FUTARI_AUDIO_WAIT_FIX");
  const bool enable=!(flag&&std::strcmp(flag,"0")==0);
  if(enable&&!period_active)period_active=timeBeginPeriod(1)==TIMERR_NOERROR;
  active=enable;
  REXLOG_INFO("AUDIO_WAIT policy enabled={} timer_1ms={} requested_worker_priority=above_normal timeout_yield=thread_scoped no_queue_flush no_guest_writes",enable,period_active);
}
void FutariStopAudioWaitPolicy() {
  active=false;
  if(period_active){timeEndPeriod(1);period_active=false;}
}
void FutariEnterAudioTimedWait(unsigned milliseconds) {
  // This hook is only in sub_821CB108, immediately before its original wait.
  // Do not treat unrelated or zero-time calls as audio compatibility waits.
  if(milliseconds!=1){worker.RestorePriority();return;}
  if(!worker.observed){
    GROUP_AFFINITY affinity{};
    const bool affinity_ok=GetThreadGroupAffinity(GetCurrentThread(),&affinity)!=0;
    PROCESSOR_NUMBER processor{};
    GetCurrentProcessorNumberEx(&processor);
    REXLOG_INFO("AUDIO_WAIT host_worker tid={} policy_active={} native_priority={} affinity_read_ok={} group={} affinity_mask={:016X} observed_cpu_group={} observed_cpu={} no_affinity_change",
        GetCurrentThreadId(),active.load(),GetThreadPriority(GetCurrentThread()),affinity_ok,
        affinity.Group,uint64_t(affinity.Mask),processor.Group,processor.Number);
    worker.observed=true;
  }
  if(!active.load(std::memory_order_relaxed)){worker.RestorePriority();return;}
  if(worker.in_wait)return;
  if(!worker.reported){
    worker.previous_priority=GetThreadPriority(GetCurrentThread());
    if(worker.previous_priority!=THREAD_PRIORITY_ERROR_RETURN && worker.previous_priority<THREAD_PRIORITY_ABOVE_NORMAL){
      worker.priority_changed=SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL)!=0;
    }
    REXLOG_INFO("AUDIO_WAIT worker tid={} original_priority={} actual_priority={} changed={}",GetCurrentThreadId(),worker.previous_priority,GetThreadPriority(GetCurrentThread()),worker.priority_changed);
    worker.reported=true;
  }
  worker.previous_yield=rex::system::ExchangeTimedWaitYieldSuppression(true);
  worker.in_wait=true;
}
void FutariLeaveAudioTimedWait() {
  if(worker.in_wait){rex::system::ExchangeTimedWaitYieldSuppression(worker.previous_yield);worker.in_wait=false;}
  // Keep only this sound worker above normal between its callbacks, too.
  // The thread owns the change; exit destroys its scope, other threads untouched.
  if(!active.load(std::memory_order_relaxed))worker.RestorePriority();
}
#else
void FutariStartAudioWaitPolicy(){}
void FutariStopAudioWaitPolicy(){}
void FutariEnterAudioTimedWait(unsigned){}
void FutariLeaveAudioTimedWait(){}
#endif
