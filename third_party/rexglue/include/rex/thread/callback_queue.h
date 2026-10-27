// Copyright 2026 EVAC PC LABEL contributors. BSD-3-Clause.
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <queue>

namespace rex::thread {
// Publication and removal are protected, including when producers enqueue
// while the consumer is idle. Callbacks must run outside the queue lock.
class CallbackQueue {
 public:
  bool empty() const { return count_.load(std::memory_order_acquire) == 0; }
  void push(std::function<void()> fn) {
    std::lock_guard lock(mutex_);
    queue_.push(std::move(fn));
    count_.store(queue_.size(), std::memory_order_release);
  }
  std::function<void()> TryPop() {
    if (empty())
      return {};
    std::lock_guard lock(mutex_);
    if (queue_.empty())
      return {};
    auto fn = std::move(queue_.front());
    queue_.pop();
    count_.store(queue_.size(), std::memory_order_release);
    return fn;
  }

 private:
  std::mutex mutex_;
  std::queue<std::function<void()>> queue_;
  std::atomic<size_t> count_{0};
};
}  // namespace rex::thread
