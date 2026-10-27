// Copyright 2026 EVAC PC LABEL contributors. BSD-3-Clause.
#pragma once

#include <chrono>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace rex::thread {

// One consumer, any number of publishers. Notify must follow publication of
// the caller's predicate. The auto-reset event retains notify-before-wait;
// callers always recheck their predicate and steady deadline after waking.
class DeadlineWait {
 public:
  using clock = std::chrono::steady_clock;
  enum class Result { kNotified, kDeadline, kUnavailable };

  DeadlineWait() {
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                    TIMER_MODIFY_STATE | SYNCHRONIZE);
  }
  ~DeadlineWait() {
    if (timer_)
      CloseHandle(timer_);
    if (event_)
      CloseHandle(event_);
  }
  DeadlineWait(const DeadlineWait&) = delete;
  DeadlineWait& operator=(const DeadlineWait&) = delete;

  bool available() const { return event_ && timer_; }
  void Notify() {
    if (event_)
      SetEvent(event_);
  }

  Result WaitUntil(clock::time_point deadline) {
    if (!available())
      return Result::kUnavailable;
    if (deadline == clock::time_point::max()) {
      return WaitForSingleObject(event_, INFINITE) == WAIT_OBJECT_0 ? Result::kNotified
                                                                    : Result::kUnavailable;
    }
    const auto now = clock::now();
    if (deadline <= now)
      return Result::kDeadline;
    // Relative 100 ns units, rounded up. Do not convert to wall clock or use a
    // millisecond timeout: the kernel timestamp callback has a 1 ms cadence.
    using ticks = std::chrono::duration<long long, std::ratio<1, 10000000>>;
    LARGE_INTEGER due;
    due.QuadPart = -std::chrono::ceil<ticks>(deadline - now).count();
    if (!SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0)) {
      return Result::kUnavailable;
    }
    HANDLE handles[] = {event_, timer_};
    const auto result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (result == WAIT_OBJECT_0)
      return Result::kNotified;
    if (result == WAIT_OBJECT_0 + 1)
      return Result::kDeadline;
    return Result::kUnavailable;
  }

 private:
  HANDLE event_ = nullptr;
  HANDLE timer_ = nullptr;
};

}  // namespace rex::thread
