#pragma once
#include "gs_scanout.h"
#include "gs_completions.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cave::ps2 {
// The runtime's command boundary. A queued implementation must copy all spans
// before returning. Only this backend owns drawing state while installed.
class GsCommandBackend {
public:
    virtual ~GsCommandBackend()=default;
    virtual void BeginCommand(uint64_t generation) {}
    virtual GsCompletions CollectCompletions(){return {};}
    virtual void AcknowledgeSignal(){}
    virtual void AcknowledgeStatus(uint32_t mask){if(mask&1u)AcknowledgeSignal();}
    virtual void Initialize(std::span<const uint8_t> vram)=0;
    virtual void Reset()=0;
    virtual void Gif(GifPathId path,std::span<const uint8_t> bytes)=0;
    virtual void Register(uint8_t address,uint64_t value)=0;
    virtual void Image(const std::array<uint64_t,4>& setup,std::span<const uint8_t> bytes)=0;
    virtual size_t ReadFifo(std::span<uint8_t> bytes)=0;
    virtual void Snapshot(std::vector<uint8_t>& bytes)=0;
    virtual void WriteVram(size_t offset,std::span<const uint8_t> bytes)=0;
    virtual void Frame(const GsFrame& request)=0;
};
}
