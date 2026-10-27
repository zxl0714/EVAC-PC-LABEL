#define NOMINMAX
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <rex/graphics/command_wait.h>
#include <rex/thread/callback_queue.h>
#include <thread>
#include <vector>
#include <windows.h>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
static void Check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}
static uint64_t Cpu() {
  FILETIME c, e, k, u;
  Check(GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u), "thread CPU time");
  return (uint64_t(k.dwHighDateTime) << 32) + k.dwLowDateTime +
         (uint64_t(u.dwHighDateTime) << 32) + u.dwLowDateTime;
}
int main() {
  rex::thread::CallbackQueue queue;
  std::atomic<int> calls{0};
  std::atomic<int> producers_done{0};
  std::vector<std::jthread> producers;
  for (int p = 0; p < 4; ++p)
    producers.emplace_back([&] {
      for (int i = 0; i < 32; ++i)
        queue.push([&] { ++calls; });
      ++producers_done;
    });
  while (producers_done != 4 || !queue.empty()) {
    if (auto fn = queue.TryPop()) fn();
    else std::this_thread::yield();
  }
  producers.clear();
  while (auto fn = queue.TryPop())
    fn();
  Check(queue.empty() && calls == 128, "concurrent callback publication");
  queue.push([&] { queue.push([&] { ++calls; }); });
  while (auto fn = queue.TryPop())
    fn();
  Check(calls == 129, "callback can enqueue without lock recursion");

  for (bool blocking : {false, true}) {
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE ack = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Check(event && ack, "events created");
    std::atomic<bool> stop{false};
    std::atomic<unsigned> write{0};
    constexpr unsigned count = 60;
    std::vector<double> wake_us(count);
    Clock::time_point sent;
    uint64_t cpu = 0, cycles = 0;
    std::atomic<int> timeouts{0};
    std::jthread consumer([&] {
      const auto before = Cpu();
      ULONG64 first_cycles, last_cycles;
      QueryThreadCycleTime(GetCurrentThread(), &first_cycles);
      unsigned read = 0;
      while (!stop) {
        rex::graphics::WaitForCommandWork(
            blocking,
            [&] { return stop || read != write.load() || !queue.empty(); },
            [&](std::chrono::milliseconds timeout) {
              // Bound test failures, not the production wait.
              auto result = WaitForSingleObject(
                  event, timeout == std::chrono::milliseconds::max()
                             ? 500
                             : DWORD(timeout.count()));
              if (result == WAIT_TIMEOUT && blocking) {
                ++timeouts;
                stop = true;
              }
            },
            [] {
              SwitchToThread();
              MemoryBarrier();
            });
        while (auto fn = queue.TryPop())
          fn();
        const auto available = write.load();
        if (available != read) {
          wake_us[read] =
              std::chrono::duration<double, std::micro>(Clock::now() - sent)
                  .count();
          read = available;
          SetEvent(ack);
        }
      }
      cpu = Cpu() - before;
      QueryThreadCycleTime(GetCurrentThread(), &last_cycles);
      cycles = last_cycles - first_cycles;
    });
    // A callback-only wake must work without a ring submission.
    queue.push([&] {
      ++calls;
      SetEvent(ack);
    });
    SetEvent(event);
    Check(WaitForSingleObject(ack, 500) == WAIT_OBJECT_0, "callback-only wake");
    for (unsigned i = 0; i < count; ++i) {
      std::this_thread::sleep_for(16ms);
      sent = Clock::now();
      write.store(i + 1); // Release/acquire orders sent and the payload.
      SetEvent(event);    // Also safe if the consumer has not entered wait.
      Check(WaitForSingleObject(ack, 500) == WAIT_OBJECT_0, "ring wake");
    }
    stop = true;
    SetEvent(event);
    consumer.join();
    Check(timeouts == 0, "no fallback timeout or lost wake");
    std::sort(wake_us.begin(), wake_us.end());
    std::printf("mode=%s handoffs=%u consumer_cpu_ms=%.3f cycles=%llu "
                "wake_us_p50=%.3f p95=%.3f max=%.3f\n",
                blocking ? "blocking" : "original", count, cpu / 10000., cycles,
                wake_us[count / 2], wake_us[count * 95 / 100], wake_us.back());
    CloseHandle(ack);
    CloseHandle(event);
  }
  Check(calls == 131, "all callbacks executed once");
}
