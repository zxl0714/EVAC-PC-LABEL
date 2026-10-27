// Exercises the production timer queue, not a replacement scheduling loop.
#include <rex/cvar.h>
#include <rex/thread/deadline_wait_win.h>
#include <rex/thread/timer_queue.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = rex::thread::TimerQueueWaitItem::clock;
using Item = rex::thread::TimerQueueWaitItem;
static unsigned checks = 0;
static void Check(bool value, const char *name) {
  ++checks;
  if (!value) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    std::exit(1);
  }
}
static uint64_t CpuTicks() {
  FILETIME creation, exit, kernel, user;
  Check(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user),
        "thread times");
  auto ticks = [](FILETIME t) {
    return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
  };
  return ticks(kernel) + ticks(user);
}
static void Cancel(const std::weak_ptr<Item> &handle) {
  if (auto item = handle.lock())
    item->Disarm();
}

int main(int argc, char **argv) {
  const bool original = argc > 1 && std::string(argv[1]) == "original";
  rex::cvar::SetFlagByName("timer_queue_blocking_wait",
                           original ? "false" : "true");
  using Wait = rex::thread::DeadlineWait;
  Wait wait;
  Check(wait.available(), "high resolution timer supported on test host");
  for (int i = 0; i < 64; ++i) {
    wait.Notify();
    Check(wait.WaitUntil(Clock::now() + 100ms) == Wait::Result::kNotified,
          "notification retained before entering wait");
  }
  auto due = Clock::now() + 2ms;
  while (Clock::now() < due) {
    Check(wait.WaitUntil(due) == Wait::Result::kDeadline, "timer wake");
  }

  std::atomic<int> cancelled_calls{0};
  auto cancelled = rex::thread::QueueTimerOnce(
      [&](void *) { ++cancelled_calls; }, nullptr, Clock::now() + 5ms);
  Cancel(cancelled);
  std::promise<void> earlier_done;
  auto earlier_future = earlier_done.get_future();
  auto late =
      rex::thread::QueueTimerOnce([](void *) {}, nullptr, Clock::now() + 2s);
  const auto earlier_due = Clock::now() + 20ms;
  std::atomic<bool> early{false};
  rex::thread::QueueTimerOnce(
      [&](void *) {
        early = Clock::now() < earlier_due;
        earlier_done.set_value();
      },
      nullptr, earlier_due);
  Check(earlier_future.wait_for(1s) == std::future_status::ready,
        "earlier insertion interrupts long deadline");
  Check(!early, "no callback before its deadline");
  Check(cancelled_calls == 0, "cancel before callback");
  Cancel(late);

  // Disarm must join an in-flight callback, without holding a queue lock.
  std::promise<void> entered, release;
  auto entered_future = entered.get_future();
  auto release_future = release.get_future();
  auto active = rex::thread::QueueTimerOnce(
      [&](void *) {
        entered.set_value();
        release_future.wait();
      },
      nullptr, Clock::now() + 1ms);
  Check(entered_future.wait_for(1s) == std::future_status::ready,
        "callback entered");
  std::atomic<bool> disarmed{false};
  std::jthread canceller([&] {
    Cancel(active);
    disarmed = true;
  });
  std::this_thread::sleep_for(5ms);
  Check(!disarmed, "external disarm waits for running callback");
  release.set_value();
  canceller.join();
  Check(disarmed, "disarm completes after callback");

  // Exercise real concurrent ring publication and retained notifications.
  constexpr int publishers = 4, per_publisher = 32;
  std::atomic<int> completed{0};
  std::promise<void> published;
  auto published_future = published.get_future();
  std::vector<std::jthread> producers;
  for (int p = 0; p < publishers; ++p) {
    producers.emplace_back([&] {
      for (int i = 0; i < per_publisher; ++i) {
        rex::thread::QueueTimerOnce(
            [&](void *) {
              if (++completed == publishers * per_publisher)
                published.set_value();
            },
            nullptr, Clock::now());
      }
    });
  }
  producers.clear();
  Check(published_future.wait_for(1s) == std::future_status::ready,
        "all concurrent publications consumed exactly once");
  Check(completed == publishers * per_publisher, "publication count");

  // 1 kHz is the production KeTimeStampBundle cadence. Record the dispatcher's
  // own CPU time and steady deadline error; do not include this test's sleeps.
  constexpr int count = 1000;
  std::vector<double> late_us(count);
  int calls = 0;
  uint64_t first_cpu = 0, last_cpu = 0;
  std::promise<void> recurring_done;
  auto recurring_future = recurring_done.get_future();
  const auto first_due = Clock::now() + 20ms;
  std::promise<std::weak_ptr<Item>> recurring_promise;
  auto recurring_handle = recurring_promise.get_future().share();
  auto recurring = rex::thread::QueueTimerRecurring(
      [&](void *) {
        const auto now = Clock::now();
        if (calls == 0)
          first_cpu = CpuTicks();
        late_us[calls] = std::chrono::duration<double, std::micro>(
                             now - (first_due + calls * 1ms))
                             .count();
        if (++calls == count) {
          last_cpu = CpuTicks();
          Cancel(recurring_handle
                     .get()); // Self-disarm must not deadlock or reschedule.
          recurring_done.set_value();
        }
      },
      nullptr, first_due, 1ms);
  recurring_promise.set_value(recurring);
  Check(recurring_future.wait_for(3s) == std::future_status::ready,
        "recurring timer completes");
  Cancel(recurring); // Also joins callback completion after the promise.
  Check(calls == count, "recurring callback count and self-disarm");
  Check(*std::min_element(late_us.begin(), late_us.end()) >= 0,
        "recurring never early");
  std::sort(late_us.begin(), late_us.end());
  std::printf("mode=%s callbacks=%d dispatcher_cpu_ms=%.3f "
              "lateness_us_p50=%.3f p95=%.3f max=%.3f checks=%u\n",
              original ? "original" : "blocking", calls,
              double(last_cpu - first_cpu) / 10000, late_us[count / 2],
              late_us[count * 95 / 100], late_us.back(), checks);
  // A cancelled long timer remains queued; global destruction must wake/join
  // the dispatcher instead of waiting for that deadline.
  auto shutdown =
      rex::thread::QueueTimerOnce([](void *) {}, nullptr, Clock::now() + 1h);
  Cancel(shutdown);
}
