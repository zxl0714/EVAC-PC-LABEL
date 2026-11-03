#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace cave::ps2 {
// Device-side de-clicking only: never changes the producer clock or queue depth.
// A missing block otherwise cuts an arbitrary sample straight to/from silence.
class PcmPlayout {
public:
    static constexpr size_t RampFrames = 64; // 1.33 ms at 48 kHz.
    void Apply(std::span<int16_t> stereo, size_t validFrames) noexcept {
        const size_t frames=stereo.size()/2;
        if(stereo.size()%2 || !frames || validFrames>frames)return;
        for(size_t i=0;i<validFrames && recovery_<RampFrames;++i,++recovery_)
            for(unsigned channel=0;channel<2;++channel)
                stereo[i*2+channel]=Scale(stereo[i*2+channel],recovery_+1,RampFrames);
        if(validFrames<frames) {
            if(validFrames) {
                const size_t count=std::min(validFrames,RampFrames);
                for(size_t i=0;i<count;++i)
                    for(unsigned channel=0;channel<2;++channel)
                        stereo[(validFrames-count+i)*2+channel]=Scale(
                            stereo[(validFrames-count+i)*2+channel],count-i-1,count);
            } else {
                const size_t count=std::min(frames,RampFrames);
                for(size_t i=0;i<count;++i)
                    for(unsigned channel=0;channel<2;++channel)
                        stereo[i*2+channel]=Scale(last_[channel],count-i-1,count);
            }
            recovery_=0;
        }
        last_={stereo[(frames-1)*2],stereo[(frames-1)*2+1]};
    }
private:
    static int16_t Scale(int16_t sample,size_t numerator,size_t denominator) noexcept {
        return int16_t(int32_t(sample)*int32_t(numerator)/int32_t(denominator));
    }
    std::array<int16_t,2> last_{};
    size_t recovery_=RampFrames;
};
}
