#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace espgaluda2::latency {
// The producer is the END of the native D3D VBlank handler, after it releases
// the guest spinlock. A wake never fabricates or advances the game's clock.
class VblankGate {
public:
    enum class Result { advanced, timeout, stopped, device_changed };
    void Start() {
        std::lock_guard lock(mutex_);
        stopped_=false; device_=0; sequence_=consumed_=0; consumer_device_=0;
    }
    void Stop() {
        {std::lock_guard lock(mutex_); stopped_=true;}
        changed_.notify_all();
    }
    void Publish(std::uint32_t device) {
        {
            std::lock_guard lock(mutex_);
            if(stopped_) return;
            device_=device;
            ++sequence_;
        }
        changed_.notify_all();
    }
    Result Wait(std::uint32_t device, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        if(stopped_) return Result::stopped;
        if(consumer_device_!=device) {
            // Starting a device must see a NEW completed VBlank, not reuse an
            // interrupt from device setup or the previous renderer instance.
            consumer_device_=device;
            consumed_=sequence_;
        }
        if(!changed_.wait_for(lock,timeout,[&] {return stopped_ || sequence_!=consumed_;}))
            return Result::timeout;
        if(stopped_) return Result::stopped;
        if(device_!=device) return Result::device_changed;
        // Collapse missed intervals, like Futari's native counter comparison.
        // Never run several catch-up game frames for several queued wakes.
        consumed_=sequence_;
        return Result::advanced;
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool stopped_{true};
    std::uint32_t device_{},consumer_device_{};
    std::uint64_t sequence_{},consumed_{};
};
}
