#include "spu_device.h"
#include "audio_trace.h"
#include "audio_capture.h"
#include "Iop_Spu2.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace cave::ps2 {
class SpuDevice::Impl {
public:
    std::vector<uint8_t> ram=std::vector<uint8_t>(2u*1024u*1024u);
    Iop::CSpuSampleCache cache;
    Iop::CSpuIrqWatcher irq;
    Iop::CSpuBase core0{ram.data(),uint32_t(ram.size()),&cache,&irq,0};
    Iop::CSpuBase core1{ram.data(),uint32_t(ram.size()),&cache,&irq,1};
    Iop::CSpu2 registers{core0,core1};
    std::array<int16_t,MaxRenderFrames*2> core1Samples{};
    unsigned tracedVoices=0;
};
SpuDevice::SpuDevice():impl(std::make_unique<Impl>()) {Reset();}
SpuDevice::~SpuDevice()=default;
void SpuDevice::Reset() {
    std::fill(impl->ram.begin(),impl->ram.end(),uint8_t{});
    impl->cache.Clear();impl->irq.Reset();
    impl->core0.Reset();impl->core1.Reset();impl->registers.Reset();
    impl->core0.SetDestinationSamplingRate(SampleRate);
    impl->core1.SetDestinationSamplingRate(SampleRate);
}
uint16_t SpuDevice::Read16(uint32_t address) const {
    if(address<0x1f900000u || address>0x1f90fffeu || (address&1u))
        throw std::out_of_range("Unaligned or out-of-range SPU2 register");
    return uint16_t(impl->registers.ReadRegister(address));
}
void SpuDevice::Write16(uint32_t address,uint16_t value) {
    if(address<0x1f900000u || address>0x1f90fffeu || (address&1u))
        throw std::out_of_range("Unaligned or out-of-range SPU2 register");
    impl->registers.WriteRegister(address,value);
    const auto offset=address&0x3ffu;
    if(audio_trace::enabled && (offset==0x1a0u || offset==0x1a2u) && value) {
        const unsigned core=(address&0x400u)?1u:0u;
        auto& device=core?impl->core1:impl->core0;
        const uint32_t keys=uint32_t(value)<<(offset==0x1a2u?16u:0u);
        for(unsigned voice=0;voice<24 && impl->tracedVoices<256;++voice) {
            if(!(keys&(1u<<voice)))continue;
            ++impl->tracedVoices;
            const auto& channel=device.GetChannel(voice);
            std::osyncstream line(std::cerr);
            line<<"[ps2-audio-voice] ms="<<audio_trace::Milliseconds()<<" core="<<core<<" voice="<<voice
                <<" control=0x"<<std::hex<<device.GetControl()<<" start=0x"<<channel.address
                <<" repeat=0x"<<channel.repeat<<" pitch=0x"<<channel.pitch
                <<" left=0x"<<uint16_t(channel.volumeLeft)<<" right=0x"<<uint16_t(channel.volumeRight)
                <<" adsr=0x"<<uint16_t(channel.adsrLevel)<<','<<uint16_t(channel.adsrRate)<<" data=";
            for(unsigned i=0;i<32;++i)line<<unsigned(impl->ram[(channel.address+i)&(impl->ram.size()-1u)])<<',';
            unsigned nonzero=0,first=4096;
            for(unsigned i=0;i<4096;++i)if(impl->ram[(channel.address+i)&(impl->ram.size()-1u)]) {
                ++nonzero;first=std::min(first,i);
            }
            line<<" blockNonzero="<<std::dec<<nonzero<<" first="<<first;
            line<<std::dec<<'\n';
        }
    }
}
size_t SpuDevice::Transfer(unsigned core,std::span<uint8_t> bytes,bool toSpu) {
    if(core>1 || (bytes.size()&3u))throw std::invalid_argument("Invalid SPU2 DMA span");
    auto& spu=core?impl->core1:impl->core0;
    size_t offset=0;
    while(offset<bytes.size()) {
        const auto words=uint32_t(std::min<size_t>((bytes.size()-offset)/4u,256u));
        const auto done=spu.ReceiveDma(bytes.data()+offset,4u,words,toSpu?1u:0u);
        if(done>words)throw std::runtime_error("SPU2 DMA consumed beyond the supplied span");
        offset+=size_t(done)*4u;
        if(done<words)break;
    }
    return offset;
}
void SpuDevice::Render(std::span<int16_t> stereo) {
    if(stereo.size()%2 || stereo.size()>MaxRenderFrames*2)
        throw std::invalid_argument("Invalid SPU2 stereo render span");
    impl->core0.Render(stereo.data(),unsigned(stereo.size()));
    audio_capture::Append(0,stereo);
    // Preserve Play!'s core enable and core-to-core AVOL routing.
    if(impl->core1.IsEnabled()) {
        impl->core1.Render(impl->core1Samples.data(),unsigned(stereo.size()));
        audio_capture::Append(1,std::span<const int16_t>(impl->core1Samples.data(),stereo.size()));
        for(size_t i=0;i<stereo.size();++i) {
            const auto volume=(i&1u)?impl->core1.m_extInputVolR:impl->core1.m_extInputVolL;
            Iop::CSpuBase::MixSamples(stereo[i],volume,&impl->core1Samples[i]);
            stereo[i]=impl->core1Samples[i];
        }
    }
    else if(audio_capture::capture) {
        std::fill_n(impl->core1Samples.data(),stereo.size(),int16_t{});
        audio_capture::Append(1,std::span<const int16_t>(impl->core1Samples.data(),stereo.size()));
    }
    audio_capture::Append(2,stereo);
}
bool SpuDevice::IrqPending() const {
    return impl->core0.GetIrqPending() || impl->core1.GetIrqPending();
}
}
