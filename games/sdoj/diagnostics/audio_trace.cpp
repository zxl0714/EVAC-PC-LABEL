#include "audio_trace_policy.h"
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/memory.h>
#include <rex/system/xmemory.h>
#include <rex/logging.h>
#include <rex/input/guest_input_frame.h>
#include <rex/ppc.h>
#include <rex/system/thread_state.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

REXCVAR_DEFINE_BOOL(sdoj_audio_trace, false, "Diagnostics",
    "Bounded disc-base sound queue and dispatch observations; no audio changes")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
using namespace sdoj::diagnostics;
struct Trace {
    std::uint64_t generation{}, serial{}, produced{}, consumed{}, unpaired{}, overwritten{};
    std::uint64_t consumer_calls{}, consumer_last_us{}, consumer_gap_max_us{}, summary_frame{};
    bool summary_emitted{};
    std::array<AudioEnqueued,128> slots;
};
std::array<Trace,2> traces;
std::mutex observer_gate;
std::atomic<unsigned> records{}, contended{};
constexpr unsigned MaxRecords = 4096;
std::atomic<std::uint64_t> dispatch_serial{};
thread_local std::uint64_t dispatch_id{}, dispatch_began_us{};
std::uint64_t Now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::span<const std::uint8_t> Read(std::uint32_t address,std::uint32_t size) {
    if (!address || !size || std::uint64_t(address)+size > 0x100000000ull) return {};
    auto* runtime=rex::Runtime::instance();
    auto* memory=runtime ? runtime->memory() : nullptr;
    auto* heap=memory ? memory->LookupHeap(address) : nullptr;
    if (!heap || heap != memory->LookupHeap(address+size-1) ||
        !(std::uint32_t(heap->QueryRangeAccess(address,address+size-1)) & 1)) return {};
    return {memory->TranslateVirtual(address),size};
}
std::optional<std::uint32_t> Word(std::uint32_t address) {
    const auto bytes=Read(address,4);
    if (bytes.size()!=4) return {};
    return (std::uint32_t(bytes[0])<<24)|(std::uint32_t(bytes[1])<<16)|
           (std::uint32_t(bytes[2])<<8)|bytes[3];
}
std::string CueName(std::uint32_t descriptor) {
    if (descriptor < 0x881A0000 || descriptor > 0x889BFFE0) return {};
    const auto pointer=Word(descriptor+24);
    if (!pointer || *pointer < 0x88000000 || *pointer >= 0x88030000) return {};
    const auto bytes=Read(*pointer,std::min(64u,0x88030000u-*pointer));
    std::string name;
    for (auto c:bytes) {
        if (!c) return name;
        if (c<32 || c>126) return {};
        name.push_back(char(c));
    }
    return {}; // Only terminated, bounded guest resource labels are logged.
}
bool Admit() {
    auto n=records.load(std::memory_order_relaxed);
    while(n<MaxRecords) if(records.compare_exchange_weak(n,n+1,std::memory_order_relaxed)) {
        if(n+1==MaxRecords) REXLOG_INFO("SDOJ_AUDIO record_limit_reached={} further_observations_disabled",MaxRecords);
        return true;
    }
    return false;
}
}

extern "C" __declspec(dllexport) bool SdojAudioTraceEnabled() { return REXCVAR_GET(sdoj_audio_trace); }
extern "C" __declspec(dllexport) void SdojAudioGuestEvent(
    unsigned module,unsigned phase,unsigned a,unsigned b,unsigned c,unsigned d,double gain,
    unsigned target,unsigned return_pc) {
    if (!REXCVAR_GET(sdoj_audio_trace) || !AudioQueue(module).slots || phase>6 ||
        records.load(std::memory_order_relaxed)>=MaxRecords) return;
    const auto now=Now(),frame=rex::input::GuestInputFrame();
    if (phase>=3 && phase<=5) {
        if (phase==4) {dispatch_id=++dispatch_serial;dispatch_began_us=now;}
        if (!Admit()) return;
        const auto name=phase==4 ? CueName(c) : std::string{};
        REXLOG_INFO("SDOJ_AUDIO t_us={} frame={} module={} phase={} a={:08X} b={:08X} c={:08X} d={:08X} gain={} target={:08X} return_pc={:08X} name='{}' dispatch={} dispatch_elapsed_us={} return_is_untyped={} no_guest_writes not_speaker_latency",
            now,frame,module,phase==3?"play_request":phase==4?"dispatch":"dispatch_return",
            a,b,c,d,gain,target,return_pc,name,phase==3?0:dispatch_id,
            phase==5 && dispatch_began_us && now>=dispatch_began_us ? now-dispatch_began_us:0,phase==5);
        return;
    }
    std::unique_lock lock(observer_gate,std::try_to_lock);
    if (!lock.owns_lock()) {++contended;return;}
    auto& trace=traces[module-1];
    if (!phase) {const auto generation=trace.generation+1;trace={};trace.generation=generation;return;}
    const auto layout=AudioQueue(module);
    const auto read=Word(layout.read),write=Word(layout.write),hold=Word(layout.hold),busy=Word(layout.busy);
    if (!read || !write || !hold || !busy || *read>=128 || *write>=128) return;
    std::optional<AudioCommand> command;
    std::optional<std::uint64_t> age;
    unsigned slot=128;
    std::uint64_t serial{};
    if (phase==6) {
        ++trace.consumer_calls;
        if(trace.consumer_last_us && now>=trace.consumer_last_us)
            trace.consumer_gap_max_us=std::max(trace.consumer_gap_max_us,now-trace.consumer_last_us);
        trace.consumer_last_us=now;
        if(trace.summary_emitted && frame>=trace.summary_frame && frame-trace.summary_frame<60) return;
        trace.summary_emitted=true;trace.summary_frame=frame;
    } else {
        const auto index=AudioSlot(module,a);
        if (!index || !(command=DecodeAudioCommand(Read(a,6)))) return;
        slot=*index;
        if (phase==1) {
            if(trace.slots[slot].serial) ++trace.overwritten;
            serial=++trace.serial;
            trace.slots[slot]={*command,now,serial};++trace.produced;
        } else {
            serial=trace.slots[slot].serial;
            age=AudioQueueAge(trace.slots[slot],*command,now);
            if(!age) ++trace.unpaired;
            trace.slots[slot]={};++trace.consumed;
        }
    }
    const auto snapshot=trace;
    lock.unlock(); // Neither formatting nor the logger runs under the observer lock.
    if(!Admit()) return;
    REXLOG_INFO("SDOJ_AUDIO t_us={} frame={} module={} generation={} phase={} slot={} serial={} cue={} kind={} volume={} priority={} channel={} queue_age_us={} read={} write={} depth_mod128={} busy={} hold={} produced={} consumed={} unpaired={} overwritten={} consumer_calls={} consumer_gap_max_us={} observer_contended={} no_guest_writes not_speaker_latency",
        now,frame,module,snapshot.generation,phase==1?"enqueued":phase==2?"head":"consumer_summary",
        slot,serial,command?command->cue:0,command?command->kind:0,command?command->volume:0,
        command?command->priority:0,command?command->channel:0,age?std::int64_t(*age):-1,
        *read,*write,(*write+128-*read)%128,*busy,*hold,snapshot.produced,snapshot.consumed,
        snapshot.unpaired,snapshot.overwritten,snapshot.consumer_calls,snapshot.consumer_gap_max_us,contended.load());
}

namespace {
struct EngineObservation {
    std::uint64_t id{}, began_us{};
    std::uint32_t owner{};
    std::optional<AudioEngineCommand> command;
};
std::atomic<std::uint64_t> engine_serial{};
thread_local EngineObservation engine_push, engine_play;
bool EngineEnabled() {
    return REXCVAR_GET(sdoj_audio_trace) && records.load(std::memory_order_relaxed)<MaxRecords;
}
void EngineEmit(const char* phase,std::uint32_t engine,std::uint32_t queue,
                const AudioEngineCommand& cmd,std::uint64_t id=0,std::uint64_t began=0,
                std::uint32_t result=0,bool returned=false,bool gain_valid=false,double gain=0,
                double factor27=0,double factor30=0,double factor31=0,std::uint32_t target=0) {
    if(!Admit()) return;
    const auto now=Now();
    const auto capacity=Word(queue?queue+8:0),write=Word(queue?queue+12:0),
               read=Word(queue?queue+16:0),count=Word(queue?queue+20:0);
    const auto* thread=rex::runtime::ThreadState::Get();
    const auto* context=thread?thread->context():nullptr;
    const auto& w=cmd.words;
    REXLOG_INFO("SDOJ_AUDIO_ENGINE t_us={} frame={} phase={} engine={:08X} queue={:08X} id={} elapsed_us={} result={:08X} return_is_untyped={} capacity={} read={} write={} raw_count={} op={} channel={} resource={:08X} descriptor={:08X} arg16={:08X} arg20={:08X} gain_bits={:08X} arg28={:08X} pitch_bits={:08X} raw36={:08X} raw40={:08X} gain_valid={} gain={} factor_f27={} factor_f30={} factor_f31={} target={:08X} return_pc={:08X} name='{}' no_guest_writes not_speaker_latency",
        now,rex::input::GuestInputFrame(),phase,engine,queue,id,began&&now>=began?now-began:0,
        result,returned,capacity?std::int64_t(*capacity):-1,read?std::int64_t(*read):-1,
        write?std::int64_t(*write):-1,count?std::int64_t(*count):-1,
        w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7],w[8],w[9],w[10],
        gain_valid,gain,factor27,factor30,factor31,target,context?std::uint32_t(context->lr):0,CueName(w[3]));
}
}

// Default-image hooks use the same init-only flag and budget as the modules.
// These do not call guest audio functions or modify registers, timeouts or queues.
void SdojAudioEngineEnqueueBegin(PPCRegister& queue,PPCRegister& command) {
    if(!EngineEnabled()) return;
    engine_push={++engine_serial,Now(),queue.u32,DecodeAudioEngineCommand(Read(command.u32,44))};
    if(engine_push.command) EngineEmit("enqueue_begin",0,queue.u32,*engine_push.command,engine_push.id);
}
void SdojAudioEngineEnqueueEnd(PPCRegister& result,PPCRegister& queue) {
    if(!EngineEnabled()) return;
    const auto prior=engine_push;engine_push={};
    if(prior.command && prior.owner==queue.u32)
        EngineEmit("enqueue_end",0,queue.u32,*prior.command,prior.id,prior.began_us,result.u32,true);
}
void SdojAudioEngineHead(PPCRegister& engine,PPCRegister& queue) {
    if(!EngineEnabled()) return;
    const auto command=DecodeAudioEngineCommand(Read(engine.u32+120,44));
    if(command) EngineEmit("head",engine.u32,queue.u32,*command);
}
void SdojAudioEnginePlay(PPCRegister& engine,PPCRegister& gain,PPCRegister& factor27,
                         PPCRegister& factor30,PPCRegister& factor31,PPCRegister& target) {
    if(!EngineEnabled()) return;
    engine_play={++engine_serial,Now(),engine.u32,DecodeAudioEngineCommand(Read(engine.u32+120,44))};
    if(engine_play.command) EngineEmit("native_play",engine.u32,0,*engine_play.command,engine_play.id,
        0,0,false,true,gain.f64,factor27.f64,factor30.f64,factor31.f64,target.u32);
}
void SdojAudioEnginePlayReturned(PPCRegister& result,PPCRegister& engine) {
    if(!EngineEnabled()) return;
    const auto prior=engine_play;engine_play={};
    if(prior.command && prior.owner==engine.u32)
        EngineEmit("native_play_return",engine.u32,0,*prior.command,prior.id,prior.began_us,result.u32,true);
}
