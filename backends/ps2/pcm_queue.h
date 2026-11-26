#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

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
        auto read=read_.load(std::memory_order_relaxed);
        const auto discarded=discardBefore_.load(std::memory_order_acquire);
        if(discarded>read) {
            read=discarded;
            surplusFrames_=0;
        }
        const auto write=write_.load(std::memory_order_acquire);
        if(lateFrames) {
            *lateFrames=LateFrames(size_t(write-read),samples.size()/2);
            read+=*lateFrames;
        } else {
            surplusFrames_=0;
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
    // Both endpoints must be paused. Keep samples already produced by the
    // guest and not yet delivered to the host, in playback order.
    std::vector<int16_t> Pending() const {
        const auto read=std::max(read_.load(),discardBefore_.load());
        const auto write=write_.load();
        std::vector<int16_t> result((write-read)*2);
        for(uint64_t i=read;i<write;++i) {
            const auto pos=(i%CapacityFrames)*2;
            result[(i-read)*2]=buffer_[pos];
            result[(i-read)*2+1]=buffer_[pos+1];
        }
        return result;
    }
    void RestorePending(std::span<const int16_t> samples) noexcept {
        write_.store(0);
        read_.store(0);
        discardBefore_.store(0);
        surplusFrames_=minimumSurplus_=0;
        Write(samples);
    }
private:
    size_t LateFrames(size_t available,size_t requested) noexcept {
        constexpr size_t sampleRate=48000,field=sampleRate/60;
        const size_t surplus=available>requested ? available-requested : 0;
        // Catch up immediately after a long pause. A shorter burst may be
        // needed intact when the guest next misses a device deadline.
        if(surplus>=4*field) {
            surplusFrames_=0;
            return surplus-field;
        }
        if(surplus<=field) {
            surplusFrames_=0;
            return 0;
        }
        minimumSurplus_=surplusFrames_ ? std::min(minimumSurplus_,surplus) : surplus;
        surplusFrames_+=requested;
        if(surplusFrames_<sampleRate) return 0;
        surplusFrames_=0;
        // Trim only the surplus that went unused for a full second.
        return surplus>2*field ? minimumSurplus_-field : 0;
    }
    std::array<int16_t,CapacityFrames*2> buffer_{};
    alignas(64) std::atomic<uint64_t> read_{};
    // Consumer-owned observations, measured in source sample frames.
    size_t surplusFrames_{},minimumSurplus_{};
    alignas(64) std::atomic<uint64_t> write_{};
    alignas(64) std::atomic<uint64_t> discardBefore_{};
};
}
