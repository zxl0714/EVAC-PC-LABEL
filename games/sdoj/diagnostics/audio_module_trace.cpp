#include <rex/ppc.h>
#include <rex/system/thread_state.h>
#include <windows.h>

namespace {
using Sink = void (*)(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,double,unsigned,unsigned);
using Enabled = bool (*)();
struct Bridge {
    unsigned module;
    Sink sink{};
    explicit Bridge(unsigned id) : module(id) {
        const auto host = GetModuleHandleW(nullptr);
        const auto enabled = reinterpret_cast<Enabled>(GetProcAddress(host, "SdojAudioTraceEnabled"));
        if (!enabled || !enabled()) return;
        sink = reinterpret_cast<Sink>(GetProcAddress(host, "SdojAudioGuestEvent"));
        if (sink) sink(module,0,0,0,0,0,0,0,0);
    }
    void Emit(unsigned phase, unsigned a=0,unsigned b=0,unsigned c=0,unsigned d=0,
              double gain=0,unsigned target=0) const {
        if (!sink) return; // Init-only false avoids even the TLS context lookup.
        const auto* thread = rex::runtime::ThreadState::Get();
        const auto* context = thread ? thread->context() : nullptr;
        sink(module,phase,a,b,c,d,gain,target,context ? std::uint32_t(context->lr) : 0);
    }
};
Bridge& Normal() { static Bridge bridge(1); return bridge; }
Bridge& Arrange() { static Bridge bridge(2); return bridge; }
}

// Observation only. All original instructions, parameters and returns still run.
#define AUDIO_OBSERVERS(prefix, bridge) \
void prefix##Enqueued(PPCRegister& slot) { bridge().Emit(1,slot.u32); } \
void prefix##Head(PPCRegister& slot) { bridge().Emit(2,slot.u32); } \
void prefix##Request(PPCRegister& channel,PPCRegister& cue,PPCRegister& volume,PPCRegister& priority) { \
    bridge().Emit(3,cue.u32,channel.u32,volume.u32,priority.u32); } \
void prefix##Dispatch(PPCRegister& cue,PPCRegister& channel,PPCRegister& descriptor, \
                      PPCRegister& bank,PPCRegister& gain,PPCRegister& target) { \
    bridge().Emit(4,cue.u32,channel.u32,descriptor.u32,bank.u32,gain.f64,target.u32); } \
void prefix##Returned(PPCRegister& value) { bridge().Emit(5,value.u32); } \
void prefix##Consumer() { bridge().Emit(6); }
AUDIO_OBSERVERS(SdojAudioNormal, Normal)
AUDIO_OBSERVERS(SdojAudioArrange, Arrange)
#undef AUDIO_OBSERVERS
