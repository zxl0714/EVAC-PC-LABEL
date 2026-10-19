#pragma once
#include <cstdint>
#include <memory>

struct NativeVblankStatus {
    std::uint64_t count{}, process_microseconds{}, tsc{}, reserved{};
    unsigned char flags{}, padding[7]{};
};
static_assert(sizeof(NativeVblankStatus) == 40);

class NativeDisplayClock {
public:
    NativeDisplayClock(std::uint64_t owner, std::uint32_t numerator, std::uint32_t denominator);
    ~NativeDisplayClock();
    NativeDisplayClock(const NativeDisplayClock&) = delete;
    NativeDisplayClock& operator=(const NativeDisplayClock&) = delete;
    void Stop();
    bool SetFlipRate(int rate);
    std::uint64_t NextFlipCounter() const;
    bool WaitVblank();
    bool WaitFlipOpportunity();
    NativeVblankStatus Snapshot() const;
    int Subscribe(std::uint64_t queue, bool flip, void* user_data);
    int Unsubscribe(std::uint64_t queue, bool flip);
    // Called only after a real completed presentation; ticks never fake flips.
    void NotifyFlipCompleted(std::int64_t argument);

private:
    friend bool DisplayClockSelfTest();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
bool DisplayClockSelfTest();
