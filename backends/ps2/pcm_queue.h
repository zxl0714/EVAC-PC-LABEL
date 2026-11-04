#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace cave::ps2 {
// One IOP producer, one audio-device consumer. Indices are stereo frames;
// partial writes leave queued audio intact and report the remaining pressure.
class PcmQueue {
public:
    // A 44.1 kHz device requesting 2048 frames needs over 2200 source frames
    // in one pull, while the guest can produce another video field in a burst.
    // This is storage headroom only: playback never waits for the ring to fill.
    static constexpr size_t CapacityFrames=8192;
    size_t Write(std::span<const int16_t> samples) noexcept {
        if(samples.size()%2)return 0;
        const auto write=write_.load(std::memory_order_relaxed);
        const auto read=read_.load(std::memory_order_acquire);
        const auto frames=std::min<size_t>(samples.size()/2,CapacityFrames-size_t(write-read));
        const auto position=size_t(write%CapacityFrames);
        const auto first=std::min(frames,CapacityFrames-position);
        if(first)std::memcpy(buffer_.data()+position*2,samples.data(),first*4);
        if(frames>first)std::memcpy(buffer_.data(),samples.data()+first*2,(frames-first)*4);
        write_.store(write+frames,std::memory_order_release);
        return frames;
    }
    size_t Read(std::span<int16_t> samples,size_t* lateFrames=nullptr) noexcept {
        if(samples.size()%2)return 0;
        auto read=std::max(read_.load(std::memory_order_relaxed),
                                 discardBefore_.load(std::memory_order_acquire));
        const auto write=write_.load(std::memory_order_acquire);
        if(lateFrames) {
            // A paused guest can generate several old video fields in one burst.
            // Leave the whole device request plus a field of jitter allowance.
            constexpr size_t field=48000/60;
            const size_t available=size_t(write-read),requested=samples.size()/2;
            *lateFrames=available>requested+2*field ? available-requested-field : 0;
            read+=*lateFrames;
        }
        const auto frames=std::min<size_t>(samples.size()/2,size_t(write-read));
        const auto position=size_t(read%CapacityFrames);
        const auto first=std::min(frames,CapacityFrames-position);
        if(first)std::memcpy(samples.data(),buffer_.data()+position*2,first*4);
        if(frames>first)std::memcpy(samples.data()+first*2,buffer_.data(),(frames-first)*4);
        read_.store(read+frames,std::memory_order_release);
        return frames;
    }
    // IOP reset may happen while the device callback is active. Let the
    // consumer discard the old epoch; the producer never writes its index.
    void DiscardFromProducer() noexcept {
        discardBefore_.store(write_.load(std::memory_order_relaxed),std::memory_order_release);
    }
private:
    std::array<int16_t,CapacityFrames*2> buffer_{};
    alignas(64) std::atomic<uint64_t> read_{};
    alignas(64) std::atomic<uint64_t> write_{};
    alignas(64) std::atomic<uint64_t> discardBefore_{};
};
}
