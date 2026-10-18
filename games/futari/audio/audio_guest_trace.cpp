// audio-r2: observe guest sound scheduling without changing guest state.
#include "audio_guest_trace.h"
#include <rex/runtime.h>
#include <rex/logging.h>
#include <windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
uint64_t Now() { return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count(); }
struct ActorObservation {
  uint32_t object=0,actor=0,flags=0,kind=0,character=0,state=0,power_state=0,data_power=0;
  bool valid=false;
};
struct Event {
  uint64_t t=0, start=0, serial=0, parent=0;
  uint32_t code=0, tid=0, a=0,b=0,c=0,d=0, read=0,write=0,gate=0,busy=0;
  uint32_t slot_state=0, slot_cue=0, slot_descriptor=0;
  uint32_t producer_return_pc=0;
  ActorObservation actor;
  std::array<uint8_t,5> command{};
};
constexpr size_t Capacity=16384;
std::array<Event,Capacity> ring;
size_t put=0,get=0,count=0;
std::mutex ring_mutex;
std::atomic<bool> enabled{false},stop{false};
std::atomic<uint64_t> dropped{0},serial{0},consumer_calls{0},consumer_empty{0},consumer_gated{0},head_wait_calls{0},engine_calls{0},engine_max{0},consumer_gap_max{0};
std::thread writer;
uint8_t* guest_base=nullptr;
thread_local std::array<Event,5> enqueue_start;
thread_local Event consume_start,bank_start;
thread_local Event power_action_start;
thread_local bool consumer_active=false;
thread_local bool consumer_logged=false;
thread_local Event last_head;
thread_local uint64_t engine_start=0,last_consumer=0,last_gate=0,last_head_emit=0,last_wait_emit=0;
thread_local uint64_t timed_wait_start=0;
std::atomic<uint64_t> timed_wait_calls{0},timed_wait_total{0},timed_wait_max{0},timed_wait_over5{0},timed_wait_over15{0},timed_wait_over100{0};

uint32_t U32(uint32_t p) { uint32_t v; std::memcpy(&v,guest_base+p,4); return _byteswap_ulong(v); }
bool Readable(uint32_t p,size_t bytes) {
  if(!p || uint64_t(p)+bytes>0x100000000ULL) return false;
  MEMORY_BASIC_INFORMATION region{};
  const auto* address=guest_base+p;
  if(!VirtualQuery(address,&region,sizeof(region)) || region.State!=MEM_COMMIT ||
     (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
  const DWORD readable=PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|
                       PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY;
  return (region.Protect&readable) &&
         uintptr_t(address)+bytes<=uintptr_t(region.BaseAddress)+region.RegionSize;
}
ActorObservation ObserveActor(uint32_t object,uint32_t actor) {
  ActorObservation out;out.object=object;out.actor=actor;
  // Rare action/state probes only. No VirtualQuery on the sound-command path.
  if(!Readable(actor,159) || (object&&!Readable(object,8))) return out;
  const auto data=U32(actor+80);
  if(!Readable(data,8)) return out;
  if(object){out.flags=U32(object);out.kind=U32(object+4);}
  out.character=guest_base[actor+158];out.state=guest_base[actor+84];
  out.power_state=U32(actor+56);out.data_power=guest_base[data+7];out.valid=true;
  return out;
}
void Snapshot(Event& e) {
  e.read=U32(0x884E010C); e.write=U32(0x884DFDF4);
  e.gate=U32(0x884E1354); e.busy=U32(0x884E10CC);
  if(e.read<128) std::memcpy(e.command.data(),guest_base+0x884E10D0+e.read*5,5);
}
void Push(const Event& e) {
  // Never wait for the writer on gameplay/audio threads.
  std::unique_lock lock(ring_mutex,std::try_to_lock);
  if(!lock.owns_lock() || count==Capacity) { ++dropped; return; }
  ring[put]=e; put=(put+1)%Capacity; ++count;
}
void Maximum(std::atomic<uint64_t>& v,uint64_t n) { auto p=v.load(); while(p<n&&!v.compare_exchange_weak(p,n)){} }
struct Pending { bool valid=false; Event e; };
std::array<Pending,128> pending;
std::array<std::array<char,40>,256> names{};

void Print(Event e) {
  if(e.code>=80&&e.code<=82) {
    const char* phase=e.code==80?"power_action_begin":e.code==81?"power_action_end":"actor_state3_death_phase";
    REXLOG_INFO("AUDIO_GAMEPLAY event t_us={} phase={} tid={} serial={} parent_serial={} capture_valid={} object={:08X} actor={:08X} action_flags={:08X} action_kind={} character_kind={} actor_state={} power_state={} data_power={} no_guest_writes",
      e.t,phase,e.tid,e.serial,e.parent,e.actor.valid,e.actor.object,e.actor.actor,
      e.actor.flags,e.actor.kind,e.actor.character,e.actor.state,e.actor.power_state,e.actor.data_power);
    return;
  }
  double age=-1;
  bool matched=false,overwrite=false;
  if(e.code==1 && e.a<128 && e.b<128) {
    auto& p=pending[e.a]; overwrite=p.valid;
    // Only correlate a completed write with matching bytes and one-step advance.
    if(e.b==((e.a+1)&127) && e.c==1) p={true,e};
    else p.valid=false;
  } else if(e.code==30 && e.read<128) {
    auto& p=pending[e.read];
    matched=p.valid && p.e.command==e.command;
    if(matched && e.t>=p.e.start){age=(e.t-p.e.start)/1000.0;e.producer_return_pc=p.e.producer_return_pc;}
  } else if(e.code==31 && e.a<128 && e.read!=e.a) pending[e.a].valid=false;
  const uint32_t id=e.code==42?e.slot_descriptor:(e.code==1||e.code==30||e.code==31||e.code==32)?e.command[0]:e.b;
  const bool queue_control=(e.code==1||e.code==30||e.code==31||e.code==32) && e.command[1]==4;
  const char* name=!queue_control&&id<256 && names[id][0]?names[id].data():"unknown";
  if(e.code==60) {
    if(e.a<256) {
      if(e.a==0){pending={};names={};}
      // Four words carry at most 16 chars of an already bounded name copy.
      std::memcpy(names[e.a].data(),&e.command[0],5);
      std::memcpy(names[e.a].data()+5,&e.b,4);
      std::memcpy(names[e.a].data()+9,&e.c,4);
      std::memcpy(names[e.a].data()+13,&e.d,4);
      names[e.a][17]=0;
      REXLOG_INFO("AUDIO_GUEST name t_us={} id={} bank={} name={}",e.t,e.a,e.slot_state,names[e.a].data());
    }
    return;
  }
  REXLOG_INFO("AUDIO_GUEST event t_us={} code={} tid={} serial={} consumer_serial={} start_us={} elapsed_ms={:.3f} a={} b={} c={} d={:08X} read={} write={} gate={} busy={} command={},{},{},{},{} name={} queue_age_ms={:.3f} matched={} overwrite={} slot_state={} slot_cue={:08X} descriptor={} producer_return_pc={:08X}",
    e.t,e.code,e.tid,e.serial,e.parent,e.start,e.start&&e.t>=e.start?(e.t-e.start)/1000.0:0.0,
    e.a,e.b,e.c,e.d,e.read,e.write,e.gate,e.busy,e.command[0],e.command[1],e.command[2],e.command[3],e.command[4],name,age,matched,overwrite,e.slot_state,e.slot_cue,e.slot_descriptor,e.producer_return_pc);
}
void Writer() {
  auto next=Clock::now()+std::chrono::seconds(1);
  uint64_t last_dropped=0;
  std::vector<Event> batch; batch.reserve(Capacity);
  while(true) {
    batch.clear();
    { std::lock_guard lock(ring_mutex); while(count){batch.push_back(ring[get]);get=(get+1)%Capacity;--count;} }
    if(dropped.load()!=last_dropped){pending={};last_dropped=dropped.load();}
    // Bounded per-cycle output. Dropped records explicitly invalidate trust.
    for(size_t i=0;i<batch.size();++i) {
      if(i<2000) Print(batch[i]); else ++dropped;
    }
    if(Clock::now()>=next || stop.load()) {
      REXLOG_INFO("AUDIO_GUEST summary t_us={} dropped_total={} consumer_calls={} empty={} gated={} head_wait_calls={} consumer_gap_max_ms={:.3f} engine_calls={} engine_max_ms={:.3f}",Now(),dropped.load(),consumer_calls.load(),consumer_empty.load(),consumer_gated.load(),head_wait_calls.load(),consumer_gap_max.load()/1000.0,engine_calls.load(),engine_max.load()/1000.0);
      next=Clock::now()+std::chrono::seconds(1);
      const auto waits=timed_wait_calls.exchange(0);
      const auto total=timed_wait_total.exchange(0);
      REXLOG_INFO("AUDIO_WAIT summary t_us={} calls={} mean_ms={:.3f} max_ms={:.3f} over5={} over15={} over100={}",Now(),waits,waits?double(total)/waits/1000.0:0.0,timed_wait_max.exchange(0)/1000.0,timed_wait_over5.exchange(0),timed_wait_over15.exchange(0),timed_wait_over100.exchange(0));
    }
    if(stop.load()) {
      // Producers are disabled before stop is set. Drain events queued while
      // this batch was being written before finishing the logging thread.
      std::lock_guard lock(ring_mutex);
      if(!count) break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}
}

extern "C" __declspec(dllexport) void FutariAudioGuestEvent(uint32_t code,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
  if(!enabled.load(std::memory_order_relaxed)) return;
  Event e; e.t=Now(); e.code=code; e.tid=GetCurrentThreadId(); e.a=a;e.b=b;e.c=c;e.d=d;
  if(code==50) {engine_start=e.t;return;}
  if(code==70){timed_wait_start=e.t;return;}
  if(code==71){
    if(timed_wait_start&&e.t>=timed_wait_start){const auto elapsed=e.t-timed_wait_start;
      ++timed_wait_calls;timed_wait_total+=elapsed;Maximum(timed_wait_max,elapsed);
      if(elapsed>5000)++timed_wait_over5;if(elapsed>15000)++timed_wait_over15;if(elapsed>100000)++timed_wait_over100;
    }
    timed_wait_start=0;return;
  }
  if(code==51) {++engine_calls; if(engine_start&&e.t>=engine_start){Maximum(engine_max,e.t-engine_start); if(e.t-engine_start>100000){e.start=engine_start;Push(e);}} engine_start=0; return;}
  if(!guest_base) return;
  if(code>=90&&code<95) {
    auto& begin=enqueue_start[code-90];
    if(begin.start && begin.a==b && begin.b==c) begin.producer_return_pc=d?a:0;
    return;
  }
  if(code==80) {
    power_action_start={};e.serial=++serial;e.actor=ObserveActor(a,b);
    power_action_start=e;Push(e);return;
  }
  if(code==81) {
    if(!power_action_start.serial || power_action_start.actor.object!=a || power_action_start.actor.actor!=b) return;
    e.serial=++serial;e.parent=power_action_start.serial;e.actor=ObserveActor(a,b);
    power_action_start={};Push(e);return;
  }
  if(code==82) {e.serial=++serial;e.actor=ObserveActor(0,a);Push(e);return;}
  if(code>=10&&code<15) {
    Snapshot(e);e.start=e.t;e.serial=++serial;enqueue_start[code-10]=e;return;
  }
  if(code>=20&&code<25) {
    const auto begin=enqueue_start[code-20]; if(!begin.start) return;
    Snapshot(e); e.code=1;e.start=begin.start;e.serial=begin.serial;e.a=begin.write;e.b=e.write;e.producer_return_pc=begin.producer_return_pc;
    e.c=0; e.d=begin.b;
    if(begin.write<128) {
      std::memcpy(e.command.data(),guest_base+0x884E10D0+begin.write*5,5);
      const uint8_t operation=code==20?4:code==21?3:code==22?2:code==23?1:0;
      e.c=e.command[1]==operation && (code==20 || (e.command[0]==uint8_t(begin.b)&&e.command[2]==uint8_t(begin.c)&&e.command[3]==uint8_t(begin.d)&&e.command[4]==uint8_t(begin.a)));
    }
    enqueue_start[code-20]={}; Push(e);return;
  }
  if(code==30) {
    consumer_active=false;
    consumer_logged=false;
    ++consumer_calls;
    if(last_consumer&&e.t>=last_consumer) Maximum(consumer_gap_max,e.t-last_consumer);
    last_consumer=e.t; Snapshot(e);consume_start=e;consume_start.start=e.t;
    if(e.gate){++consumer_gated;if(e.t-last_gate>100000){e.code=33;Push(e);last_gate=e.t;}return;}
    if(e.read==e.write){++consumer_empty;return;}
    e.serial=++serial;consume_start.serial=e.serial;consumer_active=true;
    consumer_logged=e.read!=last_head.read||e.write!=last_head.write||e.command!=last_head.command||!last_head_emit||e.t-last_head_emit>=100000;
    if(consumer_logged){last_head=e;last_head_emit=e.t;Push(e);}return;
  }
  if(code==31||code==32) {
    consumer_active=false;
    Snapshot(e);e.a=consume_start.read;e.start=consume_start.start;e.serial=consume_start.serial;
    e.command=consume_start.command;
    if(consume_start.gate || consume_start.read==consume_start.write) return;
    if(code==31&&!consumer_logged&&e.read==e.a)return;
    if(code==32){++head_wait_calls;if(!consumer_logged&&e.t-last_wait_emit<100000)return;last_wait_emit=e.t;}
    if(code==32&&a<8){e.b=a;e.c=b;e.slot_state=U32(0x885B7938+a*12);e.slot_descriptor=U32(0x885B7940+a*12);e.slot_cue=U32(0x885CBB68+a*24);}
    Push(e);return;
  }
  if(code==40) {Snapshot(e);e.start=e.t;e.serial=++serial;e.parent=consumer_active?consume_start.serial:0;bank_start=e;Push(e);return;}
  if(code==41) {Snapshot(e);e.start=bank_start.start;e.serial=bank_start.serial;
    // r1 is live, valid guest stack at the bank-call return instruction.
    e.d=U32(b+0x50);e.b=bank_start.b;e.c=bank_start.c;e.parent=bank_start.parent;
    if(c>=0x885CBB68 && c<0x885CBB68+8*24 && (c-0x885CBB68)%24==0) {
      e.slot_cue=U32(c);e.slot_state=U32(c+4);
    }
    Push(e);return;
  }
  if(code==42||code==43) {Snapshot(e);e.start=bank_start.start;e.serial=bank_start.serial;e.parent=bank_start.parent;e.slot_descriptor=bank_start.b;
    if(code==43){e.a=bank_start.a;e.b=bank_start.b;
      if(e.a<8){e.slot_cue=U32(0x885CBB68+e.a*24);e.slot_state=U32(0x885CBB6C+e.a*24);}
    }
    Push(e);return;
  }
  if(code==60) {
    // Hook is inside the name table lookup: pointers are bounded to module data.
    if(a<0x8800A024 || a>=0x8800A020+0x75C || (a-0x8800A024)%12) return;
    const uint32_t id=U32(a-4); const uint32_t name_ptr=c;
    if(id>=256 || name_ptr<0x88000000 || name_ptr>=0x88070000) return;
    std::array<char,18> name{};
    for(size_t i=0;i<17&&name_ptr+i<0x88070000;++i){name[i]=char(guest_base[name_ptr+i]);if(!name[i])break;}
    e.a=id;e.slot_state=b; std::memcpy(e.command.data(),name.data(),5);
    std::memcpy(&e.b,name.data()+5,4);std::memcpy(&e.c,name.data()+9,4);std::memcpy(&e.d,name.data()+13,4);
    Push(e);return;
  }
}
void FutariStartAudioGuestTrace() {
#if defined(FUTARI_AUDIO_DIAGNOSTIC_BUILD)
  const char* flag=std::getenv("FUTARI_AUDIO_DIAGNOSTICS");
  if(flag&&std::strcmp(flag,"0")==0) return;
  guest_base=rex::Runtime::instance()->memory()->virtual_membase();
  stop=false;enabled=true;writer=std::thread(Writer);
#if defined(FUTARI_SCOPED_AUDIO_WAIT_FIX)
  constexpr auto version="audio-r3-context-v1";
#else
  constexpr auto version="audio-r2";
#endif
  REXLOG_INFO("AUDIO_GUEST ready version={} scope=kiniro_abnormal_and_default_engine_and_wait capacity={} no_pcm no_guest_state_writes",version,Capacity);
#endif
}
void FutariStopAudioGuestTrace() {enabled=false;stop=true;if(writer.joinable())writer.join();guest_base=nullptr;}

#if defined(FUTARI_AUDIO_GUEST_TEST)
void FutariAudioGuestTestStart(uint8_t* base) {
  guest_base=base; stop=false; enabled=true; writer=std::thread(Writer);
}
uint64_t FutariAudioGuestTestDropped() {return dropped.load();}
#endif
