#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace Ketsui::GpuCore {
// All GPU-owned objects and callbacks use the same serial worker. Sync is
// reentrant on that worker (including backing-copy faults), never a second
// queue that could deadlock waiting for itself.
class GpuWorker {
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
    std::packaged_task<void()> command{std::move(operation)};
    auto done = command.get_future();
    {
      std::scoped_lock lock{mutex};
      commands.push_back(std::move(command));
    }
    wake.notify_one();
    done.get();
  }
};
} // namespace Ketsui::GpuCore
