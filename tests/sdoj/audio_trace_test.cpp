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
    std::cout<<passed<<" SDOJ audio observation checks passed\n";
}
