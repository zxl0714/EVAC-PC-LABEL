#pragma once
#include <rex/thread/guest_memory_wait.h>
#include <atomic>
#include <cstdint>

namespace cave::xbox360::performance {
// Retain notifications across the predicate/check-to-wait gap. Guest stores
// and guest locks remain owned by the original code; this only parks callers.
class SignalWait {
public:
    void Start(bool enabled) { enabled_.store(enabled, std::memory_order_release); Notify(); }
    void Stop() { enabled_.store(false, std::memory_order_release); Notify(); }
    void Notify() {
        available_.store(true, std::memory_order_release);
        sequence_.fetch_add(1, std::memory_order_release);
        rex::thread::NotifyGuestMemoryWord(&sequence_);
    }
    template<typename Pending> bool Wait(Pending pending, unsigned timeout_ms=50) {
        if (!enabled_.load(std::memory_order_acquire) ||
            !available_.load(std::memory_order_acquire)) return true;
        const auto sequence=sequence_.load(std::memory_order_acquire);
        if (!pending() || !enabled_.load(std::memory_order_acquire)) return true;
        if (rex::thread::WaitGuestMemoryWord(&sequence_,sequence,timeout_ms)) return true;
        // Unknown or slow producers must never leave the original code stuck.
        // Return to native polling until a known producer publishes again.
        available_.store(false, std::memory_order_release);
        return false;
    }
private:
    std::atomic<std::uint32_t> sequence_{};
    std::atomic<bool> enabled_{};
    std::atomic<bool> available_{};
};
}
