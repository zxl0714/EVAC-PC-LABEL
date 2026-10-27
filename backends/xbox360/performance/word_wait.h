#pragma once
#include <rex/thread/guest_memory_wait.h>
#include <atomic>
#include <cstdint>

namespace cave::xbox360::performance {
// Slow native completions remain valid. After a bounded timeout, poll natively
// until this particular producer returns; unrelated waits remain available.
class WordWait {
public:
    void Start() { available_.store(true, std::memory_order_release); }
    bool Wait(void* word, std::uint32_t stored, unsigned timeout_ms=50) {
        if (!available_.load(std::memory_order_acquire)) return true;
        if (rex::thread::WaitGuestMemoryWord(word, stored, timeout_ms)) return true;
        available_.store(false, std::memory_order_release);
        return false;
    }
    void Changed(void* word) {
        available_.store(true, std::memory_order_release);
        rex::thread::NotifyGuestMemoryWord(word);
    }
private:
    std::atomic<bool> available_{};
};
}
