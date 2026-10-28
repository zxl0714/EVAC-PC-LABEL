#include "latency_probe.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
  SetLastError(12345);
  const bool enabled = evac_latency::Initialize();
  if (GetLastError() != 12345) return 2;
  const bool overflow = argc > 1 && std::strcmp(argv[1], "overflow") == 0;
  constexpr uint64_t iterations = 100000;
  volatile uint64_t sink = 0;
  const auto begin = std::chrono::steady_clock::now();
  for (uint64_t i = 0; i < iterations; ++i) {
    sink = evac_latency::Record(evac_latency::Kind::GuestObservation, i);
  }
  const auto end = std::chrono::steady_clock::now();
  std::vector<std::thread> workers;
  for (uint64_t worker = 0; worker < 4; ++worker) {
    workers.emplace_back([=] {
      for (uint64_t i = 0; i < (overflow ? 50000 : 1000); ++i)
        evac_latency::Record(evac_latency::Kind::DriverState, worker, i);
    });
  }
  for (auto& worker : workers) worker.join();
  SetLastError(12345);
  evac_latency::Flush();
  if (GetLastError() != 12345) return 3;
  if (evac_latency::Record(evac_latency::Kind::Startup) != 0) return 4;
  std::printf("enabled=%u iterations=%llu record_ns=%.3f sink=%llu\n", enabled,
      static_cast<unsigned long long>(iterations),
      std::chrono::duration<double, std::nano>(end - begin).count() / iterations,
      static_cast<unsigned long long>(sink));
}
