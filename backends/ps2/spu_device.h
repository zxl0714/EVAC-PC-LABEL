#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace cave::ps2 {
// Guest-facing SPU2 registers and sample RAM. Called by the IOP owner thread;
// audio-device queueing belongs to the host and does not access this object.
class SpuDevice {
public:
    static constexpr uint32_t SampleRate=48000;
    static constexpr size_t MaxRenderFrames=256;
    SpuDevice();
    ~SpuDevice();
    SpuDevice(const SpuDevice&)=delete;
    SpuDevice& operator=(const SpuDevice&)=delete;
    void Reset();
    uint16_t Read16(uint32_t address) const;
    void Write16(uint32_t address,uint16_t value);
    // Returns transferred bytes. A block-mode stream can apply backpressure.
    size_t Transfer(unsigned core,std::span<uint8_t> bytes,bool toSpu);
    void Render(std::span<int16_t> stereo);
    bool IrqPending() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
