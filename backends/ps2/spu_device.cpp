#include "spu_device.h"
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
    // Preserve Play!'s core enable and core-to-core AVOL routing.
    if(impl->core1.IsEnabled()) {
        impl->core1.Render(impl->core1Samples.data(),unsigned(stereo.size()));
        for(size_t i=0;i<stereo.size();++i) {
            const auto volume=(i&1u)?impl->core1.m_extInputVolR:impl->core1.m_extInputVolL;
            Iop::CSpuBase::MixSamples(stereo[i],volume,&impl->core1Samples[i]);
            stereo[i]=impl->core1Samples[i];
        }
    }
}
bool SpuDevice::IrqPending() const {
    return impl->core0.GetIrqPending() || impl->core1.GetIrqPending();
}
}
