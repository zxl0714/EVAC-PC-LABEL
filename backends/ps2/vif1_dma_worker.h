#pragma once

#include "runtime/ps2_memory.h"
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <xmmintrin.h>

namespace cave::ps2 {

// The submitting EE thread waits until the entire transfer has retired. This
// keeps RAM, VU state, GIF ordering and the DMA interrupt on the same timeline.
class Vif1DmaWorker {
    PS2Memory& memory;
    Vif1DmaWorker* previous;
    inline static thread_local Vif1DmaWorker* current = nullptr;
    std::mutex mutex;
    std::condition_variable signal;
    bool quit = false, pending = false, result = false;
    uint32_t control = 0;
    unsigned floatControl = 0;
    std::exception_ptr failure;
    std::thread thread;

    void Run() {
        std::unique_lock lock(mutex);
        for (;;) {
            signal.wait(lock, [&]{ return quit || pending; });
            if (quit) return;
            lock.unlock();
            _mm_setcsr(floatControl);
            try {
                result = memory.writeIORegister(0x10009000u, control);
            } catch (...) {
                failure = std::current_exception();
            }
            floatControl = _mm_getcsr();
            lock.lock();
            pending = false;
            signal.notify_one();
        }
    }

public:
    explicit Vif1DmaWorker(PS2Memory& owner)
        : memory(owner), previous(current), thread([this]{ Run(); }) { current = this; }
    ~Vif1DmaWorker() {
        current = previous;
        {
            std::lock_guard lock(mutex);
            quit = true;
        }
        signal.notify_one();
        thread.join();
    }

    static Vif1DmaWorker* For(PS2Memory& owner) {
        return current && &current->memory == &owner ? current : nullptr;
    }

    bool Submit(uint32_t chcr) {
        std::unique_lock lock(mutex);
        control = chcr;
        floatControl = _mm_getcsr();
        pending = true;
        signal.notify_one();
        signal.wait(lock, [&]{ return !pending; });
        _mm_setcsr(floatControl);
        if (failure) {
            auto error = failure;
            failure = nullptr;
            std::rethrow_exception(error);
        }
        return result;
    }
};

}
