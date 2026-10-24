#include "diagnostics/audio_trace_policy.h"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace sdoj::diagnostics;
    unsigned passed{};
    auto check=[&](bool result,const char* name) {if(!result)throw std::runtime_error(name);++passed;};
    check(AudioQueue(1).slots==0x886B1630 && AudioQueue(2).slots==0x886BB0B8 && !AudioQueue(3).slots,
          "disc module queues are separate");
    check(AudioQueue(1).read==AudioQueue(1).slots-36 && AudioQueue(2).read==AudioQueue(2).slots-32,
          "second module header is not guessed from first");
    check(AudioSlot(1,AudioQueue(1).slots)==0 && AudioSlot(2,AudioQueue(2).slots+127*6)==127,
          "first and last valid slot");
    check(!AudioSlot(1,AudioQueue(1).slots-6) && !AudioSlot(1,AudioQueue(1).slots+768) &&
          !AudioSlot(1,AudioQueue(1).slots+1) && !AudioSlot(3,0),"unaligned and out-of-range slot rejected");
    const std::array<std::uint8_t,6> bytes{0x12,0x34,1,0x7f,0xfe,7};
    const auto value=DecodeAudioCommand(bytes);
    check(value && value->cue==0x1234 && value->kind==1 && value->volume==127 &&
          value->priority==254 && value->channel==7,"exact big-endian six-byte sound command");
    check(!DecodeAudioCommand(std::span(bytes).first(5)) && !DecodeAudioCommand({}),"truncated command rejected");
    AudioEnqueued prior{*value,100,1};
    check(AudioQueueAge(prior,*value,116766)==116666,"command age remains wall time");
    auto changed=*value;changed.cue^=1;
    check(!AudioQueueAge(prior,changed,200) && !AudioQueueAge(prior,*value,99),"mismatch and backwards clock rejected");
    prior.serial=0;
    check(!AudioQueueAge(prior,*value,200),"unobserved slot cannot claim zero delay");
    std::array<std::uint8_t,44> engine_bytes{};
    for(unsigned n=0;n<engine_bytes.size();++n) engine_bytes[n]=std::uint8_t(n);
    const auto engine=DecodeAudioEngineCommand(engine_bytes);
    check(engine && engine->words[0]==0x00010203 && engine->words[1]==0x04050607 &&
          engine->words[10]==0x28292a2b,"all eleven engine command words decoded big endian");
    check(!DecodeAudioEngineCommand(std::span(engine_bytes).first(43)) &&
          !DecodeAudioEngineCommand({}),"truncated engine command rejected");
    engine_bytes[15]^=1;
    check(*DecodeAudioEngineCommand(engine_bytes)!=*engine,"engine descriptor changes are not merged");
    AudioDuration duration;
    duration.Add(0,100); duration.Add(100,99);
    check(!duration.count,"missing or backward duration rejected");
    duration.Add(100,5100); duration.Add(100,10101);
    check(duration.count==2 && duration.total_us==15001 && duration.max_us==10001 &&
          duration.over_5ms==1 && duration.over_10ms==1,"duration thresholds use strict upper bound");
    AudioWorkerTiming worker;
    worker.Start(100); worker.BankBegin(110); worker.BankEnd(130);
    worker.WaitBegin(200); worker.WaitEnd(1200);
    check(worker.work.total_us==100 && worker.bank.total_us==20 && worker.wait.total_us==1000 &&
          !worker.invalid,"work includes bank; wait separate");
    worker.Start(1300); worker.WaitBegin(1350); worker.WaitEnd(14350);
    check(worker.gap.max_us==1200 && worker.wait.over_10ms==1,"loop gap and long waits tracked");
    check(!worker.Ready(99) && !worker.Ready(1000099) && worker.Ready(1000100),"bounded wall-time summary cadence");
    worker.Roll(1000100);
    check(!worker.work.count && !worker.bank.count && !worker.wait.count && !worker.gap.count &&
          worker.previous_loop_us==1300 && !worker.invalid,"summary resets counters not loop origin");
    worker.Start(1000200);
    check(worker.gap.max_us==998900,"logger/preemption between iterations remains visible");
    worker.Start(1000300); worker.BankEnd(1000400);
    check(worker.invalid==2,"unpaired phases are counted not silently called zero");
    worker={};
    check(!worker.previous_loop_us && !worker.window_us,"new worker does not inherit previous worker");
    std::cout<<passed<<" SDOJ audio observation checks passed\n";
}
