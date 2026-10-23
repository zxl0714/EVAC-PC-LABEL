#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include "diagnostics/timing_trace.h"

namespace Ketsui::GpuCore {
// All GPU-owned objects and callbacks use the same serial worker. Sync is
// reentrant on that worker (including backing-copy faults), never a second
// queue that could deadlock waiting for itself.
class GpuWorker {
public:
  struct Timing {
    std::uint64_t began{}, enqueued{}, started{}, finished{}, resumed{}, cpu_100ns{};
    std::uint32_t caller{}, worker{}, depth{};
  };
  using Observer = void (*)(const Timing&) noexcept;
  // Set before publishing this worker to callers.
  void SetObserver(Observer value) { observer = value; }
private:
  Observer observer{};
  std::mutex mutex;
  std::condition_variable_any wake;
  std::deque<std::packaged_task<void()>> commands;
  std::jthread worker{[this](std::stop_token stop) {
    while (!stop.stop_requested()) {
      std::unique_lock lock{mutex};
      wake.wait(lock, stop, [this] { return !commands.empty(); });
      if (stop.stop_requested()) {
        break;
      }
      auto command = std::move(commands.front());
      commands.pop_front();
      lock.unlock();
      command();
    }
  }};

public:
  ~GpuWorker() { worker.request_stop(); }
  bool IsCurrent() const {
    return worker.get_id() == std::this_thread::get_id();
  }
  void Sync(std::function<void()> operation) {
    if (IsCurrent()) {
      operation();
      return;
    }
    Timing timing;
    const auto report = observer;
    if (report) { timing.began = kt::diagnostics::MonotonicMicroseconds(); timing.caller = GetCurrentThreadId(); }
    std::packaged_task<void()> command{[&, operation = std::move(operation)] {
      std::uint64_t cpu{};
      if (report) {
        timing.started = kt::diagnostics::MonotonicMicroseconds();
        timing.worker = GetCurrentThreadId(); cpu = kt::diagnostics::ThreadCpu100ns();
      }
      try { operation(); }
      catch (...) {
        if (report) { timing.cpu_100ns = kt::diagnostics::ThreadCpu100ns()-cpu; timing.finished = kt::diagnostics::MonotonicMicroseconds(); }
        throw;
      }
      if (report) { timing.cpu_100ns = kt::diagnostics::ThreadCpu100ns()-cpu; timing.finished = kt::diagnostics::MonotonicMicroseconds(); }
    }};
    auto done = command.get_future();
    {
      std::scoped_lock lock{mutex};
      commands.push_back(std::move(command));
      if (report) { timing.enqueued = kt::diagnostics::MonotonicMicroseconds(); timing.depth = static_cast<std::uint32_t>(commands.size()); }
    }
    wake.notify_one();
    try { done.get(); }
    catch (...) {
      if (report) { timing.resumed = kt::diagnostics::MonotonicMicroseconds(); report(timing); }
      throw;
    }
    if (report) { timing.resumed = kt::diagnostics::MonotonicMicroseconds(); report(timing); }
  }
};
} // namespace Ketsui::GpuCore
