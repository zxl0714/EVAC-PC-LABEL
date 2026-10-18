#include <rex/ppc.h>
#include <windows.h>
#if defined(FUTARI_AUDIO_DIAGNOSTIC_BUILD)
#include <rex/system/thread_state.h>
#endif
#if defined(FUTARI_SCOPED_AUDIO_WAIT_FIX)
#include "audio_wait_policy.h"
#endif

namespace {
void Emit(uint32_t code,PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) {
#if defined(FUTARI_AUDIO_DIAGNOSTIC_BUILD)
  using Sink=void(__cdecl*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
  static const auto sink=reinterpret_cast<Sink>(GetProcAddress(GetModuleHandleW(nullptr),"FutariAudioGuestEvent"));
  if(sink) {
    sink(code,a.u32,b.u32,c.u32,d.u32);
    if(code>=10 && code<15) {
      // The generated function uses this thread's PPCContext. Get() is a safe
      // nullable TLS lookup; GetCurrentThread/current_ppc_context would assert
      // in the standalone hook test. LR is a return PC, not always LR-4's direct
      // enqueue call (tail-call sound wrappers keep their parent's LR).
      const auto* thread=rex::runtime::ThreadState::Get();
      const auto* context=thread?thread->context():nullptr;
      sink(code+80,context?uint32_t(context->lr):0,a.u32,b.u32,context?1:0);
    }
  }
#endif
}
}
void FutariAudioEnqueue4Begin(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(10,a,b,c,d); }
void FutariAudioEnqueue4End(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(20,a,b,c,d); }
void FutariAudioEnqueue3Begin(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(11,a,b,c,d); }
void FutariAudioEnqueue3End(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(21,a,b,c,d); }
void FutariAudioEnqueue2Begin(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(12,a,b,c,d); }
void FutariAudioEnqueue2End(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(22,a,b,c,d); }
void FutariAudioEnqueue1Begin(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(13,a,b,c,d); }
void FutariAudioEnqueue1End(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(23,a,b,c,d); }
void FutariAudioEnqueue0Begin(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(14,a,b,c,d); }
void FutariAudioEnqueue0End(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(24,a,b,c,d); }
void FutariAudioConsumeBegin(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(30,a,b,c,d); }
void FutariAudioConsumeEnd(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(31,a,b,c,d); }
void FutariAudioConsumeHeadWait(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(32,a,b,c,d); }
void FutariAudioCueRequest(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(40,a,b,c,d); }
void FutariAudioCueBankCall(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(42,a,b,c,d); }
void FutariAudioCueBankResult(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(41,a,b,c,d); }
void FutariAudioCueRequestEnd(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(43,a,b,c,d); }
void FutariAudioCueName(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(60,a,b,c,d); }
void FutariAudioPowerActionBegin(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(80,a,b,c,d); }
void FutariAudioPowerActionEnd(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(81,a,b,c,d); }
void FutariAudioDeathPhase(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(82,a,b,c,d); }
void FutariAudioEngineWorkBegin(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(50,a,b,c,d); }
void FutariAudioEngineWorkEnd(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) { Emit(51,a,b,c,d); }
void FutariAudioWaitBegin(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) {
#if defined(FUTARI_SCOPED_AUDIO_WAIT_FIX)
  FutariEnterAudioTimedWait(b.u32);
#endif
  Emit(70,a,b,c,d);
}
void FutariAudioWaitEnd(PPCRegister& a,PPCRegister& b,PPCRegister& c,PPCRegister& d) {
#if defined(FUTARI_SCOPED_AUDIO_WAIT_FIX)
  FutariLeaveAudioTimedWait();
#endif
  Emit(71,a,b,c,d);
}

