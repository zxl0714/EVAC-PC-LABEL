#include "native_offline_bench.h"
#include "diagnostics/monotonic_clock.h"
#include "native_async_log.h"
#include "native_display_clock.h"
#include "native_gpu.h"
#include "native_gnm.h"
#include "native_memory.h"
#include "native_resident_gpu.h"
#include "native_semaphore.h"
#include "native_threads.h"
#include "runtime_api.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <latch>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <windows.h>

namespace {
using u64 = std::uint64_t;
using u32 = std::uint32_t;
using kt::diagnostics::MonotonicMicroseconds;
using namespace Ketsui::GpuCore;
#define BENCH_SYSV __attribute__((sysv_abi))
void Require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
struct Watchdog {
  std::mutex mutex;
  std::condition_variable_any wake;
  std::jthread thread{[this](std::stop_token stop) {
    std::unique_lock lock(mutex);
    wake.wait_for(lock, stop, std::chrono::seconds(600), [] { return false; });
    if (!stop.stop_requested()) {
      std::fputs("Offline benchmark timed out\n", stderr);
      std::abort();
    }
  }};
  ~Watchdog() { thread.request_stop(); }
};
struct LogScope {
  bool active = StartNativeAsyncLog();
  LogScope() { Require(active, "Cannot start bounded benchmark diagnostics"); }
  void Stop() {
    if (active) {
      Require(StopNativeAsyncLog(), "Benchmark diagnostics did not drain");
      active = false;
    }
  }
  ~LogScope() {
    if (active) {
      (void)StopNativeAsyncLog();
    }
  }
};
struct Library {
  HMODULE handle{};
  RuntimeApi api{};
  explicit Library(const std::filesystem::path &path) {
    Require(path.is_absolute() && std::filesystem::is_regular_file(path),
            "An absolute paired GPU DLL is required");
    handle = LoadLibraryExW(path.c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
    Require(handle != nullptr, "Cannot load benchmark GPU DLL");
    const auto get = reinterpret_cast<GetRuntimeApi>(
        GetProcAddress(handle, "KetsuiGpuRuntimeGetApi"));
    if (!get || !get(RuntimeApiVersion, sizeof(api), &api)) {
      FreeLibrary(handle);
      handle = nullptr;
      throw std::runtime_error("Benchmark GPU API mismatch");
    }
  }
  ~Library() {
    if (handle) {
      FreeLibrary(handle);
    }
  }
};
constexpr u64 base = 0x4000000000, allocation_bytes = 0x100000;
struct Allocation {
  using Allocate = int(BENCH_SYSV *)(std::int64_t, std::int64_t, u64, u64, int,
                                     std::int64_t *);
  using Map = int(BENCH_SYSV *)(void **, u64, int, int, std::int64_t, u64);
  using Release = int(BENCH_SYSV *)(u64, u64);
  std::int64_t physical = -1;
  unsigned char *data{};
  Allocation() {
    const auto allocate =
        reinterpret_cast<Allocate>(ResolveMemoryImport("rTXw65xmLIA"));
    const auto map = reinterpret_cast<Map>(ResolveMemoryImport("L-Q3LEjIbgA"));
    Require(allocate(0, 0x100000000ull, allocation_bytes, 0x10000, 3,
                     &physical) == 0,
            "Benchmark memory allocation failed");
    void *view = reinterpret_cast<void *>(base);
    if (map(&view, allocation_bytes, 0x33, 0x90, physical, 0) != 0 ||
        view != reinterpret_cast<void *>(base)) {
      reinterpret_cast<Release>(ResolveMemoryImport("MBuItvba6z8"))(
          static_cast<u64>(physical), allocation_bytes);
      physical = -1;
      throw std::runtime_error("Benchmark memory mapping failed");
    }
    data = static_cast<unsigned char *>(view);
    std::memset(data, 0, allocation_bytes);
  }
  ~Allocation() {
    if (physical >= 0 &&
        reinterpret_cast<Release>(ResolveMemoryImport("MBuItvba6z8"))(
            static_cast<u64>(physical), allocation_bytes)) {
      std::terminate();
    }
  }
};
void Program(unsigned char *destination, bool vertex, bool green = false) {
  // Project-authored point/color GCN, shared in origin with runtime_smoke.cpp.
  // No extracted game shader or data is embedded in this benchmark.
  const std::vector<u32> words =
      vertex ? std::vector<u32>{0xbeeb03ff, 7,          0x7e000280,
                                0x7e0202f2, 0xf80000cf, 0x01000000,
                                0xf80008d1, 1,          0xbf810000}
             : std::vector<u32>{0xbeeb03ff, 7,
                                0x7e0002f2, 0x7e020280,
                                0xf800180f, green ? 0x00010001u : 0x00010100u,
                                0xbf810000};
  std::array<unsigned char, 128> blob{};
  std::memcpy(blob.data(), words.data(), words.size() * 4);
  std::memcpy(blob.data() + 64, "OrbShdr\7", 8);
  const u32 info =
      (static_cast<u32>(words.size() * 4) << 8) | (vertex ? 5u : 1u);
  const u64 hash = vertex  ? 0x1020304050607080ull
                   : green ? 0x8172635445362718ull
                           : 0x123456789abcdef0ull;
  std::memcpy(blob.data() + 72, &info, 4);
  std::memcpy(blob.data() + 80, &hash, 8);
  std::memcpy(destination, blob.data(), blob.size());
}
void Packet(std::vector<u32> &stream, u32 opcode, std::span<const u32> words) {
  Require(!words.empty() && words.size() <= 0x4000,
          "Invalid authored packet extent");
  stream.push_back(0xc0000000u | (static_cast<u32>(words.size() - 1) << 16) |
                   (opcode << 8));
  stream.insert(stream.end(), words.begin(), words.end());
}
void Register(std::vector<u32> &stream, u32 offset,
              std::span<const u32> words) {
  const auto bank = offset >= 0xc000   ? 0xc000u
                    : offset >= 0xa000 ? 0xa000u
                                       : 0x2c00u;
  Require(offset >= bank && offset - bank + words.size() <= 1024,
          "Authored register range is invalid");
  std::vector<u32> body{offset - bank};
  body.insert(body.end(), words.begin(), words.end());
  Packet(stream, bank == 0xc000 ? 0x79 : bank == 0xa000 ? 0x69 : 0x76, body);
}
void Word(std::vector<u32> &stream, u32 offset, u32 word) {
  Register(stream, offset, {&word, 1});
}
void Shader(std::vector<u32> &stream, u32 offset, u64 address) {
  const std::array<u32, 4> words{static_cast<u32>(address >> 8),
                                 static_cast<u32>(address >> 40), 0, 0};
  Register(stream, offset, words);
}
std::vector<u32> Setup(const RuntimeRegisterLayout &layout) {
  std::vector<u32> stream;
  Shader(stream, layout.vs_program, base + 0x10000);
  Shader(stream, layout.ps_program, base + 0x20000);
  const std::array<u32, 6> color{
      static_cast<u32>(base >> 8), 7, 63, 0, 0x8028, 8 | (8 << 5)};
  Register(stream, layout.color_buffers, color);
  Word(stream, layout.color_target_mask, 15);
  Word(stream, layout.color_shader_mask, 15);
  Word(stream, layout.color_control, (0xcc << 16) | (1 << 4));
  Word(stream, layout.color_export_format, 9);
  Word(stream, layout.shader_pos_format, 4 | (1 << 4));
  Word(stream, layout.vs_output_control, (1 << 16) | (1 << 21));
  Word(stream, layout.primitive_type, 1);
  const auto f = [](float value) { return std::bit_cast<u32>(value); };
  const std::array<u32, 6> viewport{f(32), f(32),   f(32),
                                    f(32), f(0.5f), f(0.5f)};
  const std::array<u32, 2> scissor{0, 64 | (64 << 16)}, depth{f(0), f(1)};
  Register(stream, layout.viewports, viewport);
  Word(stream, layout.viewport_control, 0x3f);
  Register(stream, layout.screen_scissor, scissor);
  Register(stream, layout.window_scissor, scissor);
  Register(stream, layout.generic_scissor, scissor);
  Register(stream, layout.viewport_depths, depth);
  return stream;
}
std::vector<u32> Workload(const RuntimeRegisterLayout &layout, unsigned draws,
                          unsigned prefetches, bool green) {
  std::vector<u32> stream;
  Shader(stream, layout.ps_program, base + (green ? 0x30000 : 0x20000));
  const std::array<u32, 1> instances{1};
  Packet(stream, 0x2f, instances);
  for (unsigned index = 0; index < draws; ++index) {
    if (index == 8) {
      const std::array<u32, 6> acquire{0x2ec47fc0, 0xffffffff, 0, 0, 0, 10};
      Packet(stream, 0x58, acquire);
    }
    if (index < prefetches) {
      const auto source =
          base + ((index % 2) ? 0x10000 : (green ? 0x30000 : 0x20000));
      const std::array<u32, 6> dma{0x60000000,
                                   static_cast<u32>(source),
                                   static_cast<u32>(source >> 32),
                                   0x3022c,
                                   0,
                                   0x28200080};
      Packet(stream, 0x50, dma);
    }
    const std::array<u32, 2> draw{1, 2};
    Packet(stream, 0x2d, draw);
  }
  std::array<u32, 63> flip{};
  flip[0] = 0x68750777;
  Packet(stream, 0x10, flip);
  return stream;
}
struct GpuSample {
  u64 snapshot{}, execute{}, completion{}, verification{}, total{};
  NativeGpuExecution command;
};
struct GpuCase {
  const char *name;
  unsigned draws, prefetches;
  u64 first_iteration_us{};
  std::vector<GpuSample> samples;
};
GpuCase GpuBenchmark(NativeResidentGpu &gpu,
                     const RuntimeRegisterLayout &registers, unsigned draws,
                     unsigned prefetches, const char *name, unsigned samples,
                     unsigned warmup, const Allocation &allocation) {
  GpuCase result{name, draws, prefetches, 0, {}};
  result.samples.reserve(samples);
  const auto display =
      kt::gfx::DescribeDisplay({0x80002200, 1, 0, 64, 64, 64, 0, 0, 0}, false);
  Require(display.error == kt::gfx::LayoutError::None,
          "Authored display is invalid");
  std::array<std::atomic<u64>, 1> labels{};
  NativeVideoGpuContext target{
      nullptr, display.layout, {allocation.data, 64 * 64 * 4},
      base,    labels,         &gpu};
  kt::gfx::pm4::Analysis state;
  const std::array<std::vector<u32>, 1> setup{Setup(registers)};
  gpu.Run([&] {
    gpu.Clear();
    gpu.Extent(0, 64, 64);
    (void)ExecuteNativeGpuCommands(target, setup, state, nullptr);
  });
  const std::array<std::vector<u32>, 2> streams{
      Workload(registers, draws, prefetches, false),
      Workload(registers, draws, prefetches, true)};
  for (unsigned iteration = 0; iteration < samples + warmup; ++iteration) {
    GpuSample row;
    const auto start = MonotonicMicroseconds();
    const std::array<std::vector<u32>, 1> snapshot{streams[iteration % 2]};
    const auto copied = MonotonicMicroseconds();
    row.snapshot = copied - start;
    gpu.Run([&] {
      row.command = ExecuteNativeGpuCommands(target, snapshot, state, nullptr);
    });
    const auto executed = MonotonicMicroseconds();
    row.execute = executed - copied;
    gpu.Drain();
    const auto completed = MonotonicMicroseconds();
    row.completion = completed - executed;
    std::array<u32, 64 * 64> pixels{};
    gpu.ReadColorForDiagnostics(base, 64, 64, pixels);
    unsigned colored{};
    for (unsigned index = 0; index < pixels.size(); ++index) {
      if (!pixels[index]) {
        continue;
      }
      Require(pixels[index] == (iteration % 2 ? 0xff00ff00u : 0xff0000ffu) &&
                  index % 64 >= 31 && index % 64 <= 32 && index / 64 >= 31 &&
                  index / 64 <= 32,
              "Authored GPU pixel output mismatch");
      ++colored;
    }
    Require(colored == 1, "Authored GPU output must contain exactly one pixel");
    row.verification = MonotonicMicroseconds() - completed;
    row.total = MonotonicMicroseconds() - start;
    Require(row.command.draws == draws && row.command.acquires == 1 &&
                row.command.prefetches == prefetches &&
                row.command.prepared_flip,
            "Benchmark did not execute its declared command workload");
    Require(row.execute >= row.command.duration_us,
            "Invalid worker timing nesting");
    if (!iteration) {
      result.first_iteration_us = row.total;
    }
    if (iteration >= warmup) {
      result.samples.push_back(row);
    }
  }
  return result;
}
using CreateSema = int(BENCH_SYSV *)(u64 *, const char *, unsigned, int, int,
                                     const void *);
using WaitSema = int(BENCH_SYSV *)(u64, int, unsigned *);
using SignalSema = int(BENCH_SYSV *)(u64, int);
using DeleteSema = int(BENCH_SYSV *)(u64);
struct SemaphorePool {
  CreateSema create =
      reinterpret_cast<CreateSema>(ResolveSemaphoreImport("188x57JYp0g"));
  WaitSema wait =
      reinterpret_cast<WaitSema>(ResolveSemaphoreImport("Zxa0VhQVTsk"));
  SignalSema signal =
      reinterpret_cast<SignalSema>(ResolveSemaphoreImport("4czppHBiriw"));
  DeleteSema remove =
      reinterpret_cast<DeleteSema>(ResolveSemaphoreImport("R1Jvn8bSCW8"));
  u64 work{}, done{};
  std::atomic<bool> stop{}, failed{};
  std::atomic<u64> tasks{}, checksum{};
  std::vector<std::jthread> threads;
  std::latch started{6};
  std::array<u64, 6> masks{};
  u64 creator_mask{};
  static u64 Task(u64 ticket) {
    u64 value = ticket + 1;
    for (unsigned round = 0; round < 32; ++round) {
      value = (value ^ (value >> 7)) * 0x9e3779b97f4a7c15ull;
    }
    return value;
  }
  explicit SemaphorePool(bool pin_workers) {
    try {
      GROUP_AFFINITY creator{};
      Require(GetThreadGroupAffinity(GetCurrentThread(), &creator) &&
                  creator.Group == 0 && creator.Mask != 0,
              "Cannot read authored worker creator affinity");
      creator_mask = creator.Mask;
      Require(create(&work, "bench-work", 1, 0, 64, nullptr) == 0 &&
                  create(&done, "bench-done", 1, 0, 64, nullptr) == 0,
              "Cannot create authored semaphores");
      for (unsigned worker = 0; worker < 6; ++worker) {
        masks[worker] = pin_workers
                            ? NativeHostAffinityForDiagnostics(u64{1} << worker)
                            : creator_mask;
        Require(masks[worker] != 0,
                "Current host cannot map six virtual application CPUs");
        threads.emplace_back([this, worker, pin_workers] {
          GROUP_AFFINITY actual{};
          // Explicit bench-only control: retain the creator's inherited mask.
          // Never broaden it, touch production threads, or change priority.
          if ((pin_workers &&
               !SetThreadAffinityMask(GetCurrentThread(), masks[worker])) ||
              !GetThreadGroupAffinity(GetCurrentThread(), &actual) ||
              actual.Group != 0 || actual.Mask != masks[worker]) {
            failed = true;
            started.count_down();
            return;
          }
          started.count_down();
          for (;;) {
            if (wait(work, 1, nullptr) != 0) {
              failed = true;
              return;
            }
            if (stop.load()) {
              return;
            }
            checksum.fetch_add(Task(tasks.fetch_add(1)),
                               std::memory_order_relaxed);
            if (signal(done, 1) != 0) {
              failed = true;
              return;
            }
          }
        });
      }
      started.wait();
      Require(!failed, "Cannot apply authored worker mapping");
    } catch (...) {
      Stop();
      throw;
    }
  }
  void Stop() noexcept {
    stop = true;
    if (work && !threads.empty()) {
      (void)signal(work, static_cast<int>(threads.size()));
    }
    for (auto &thread : threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }
    threads.clear();
    if (work) {
      (void)remove(work);
      work = 0;
    }
    if (done) {
      (void)remove(done);
      done = 0;
    }
  }
  ~SemaphorePool() { Stop(); }
};
struct HandoffCase {
  unsigned tasks_per_batch{};
  bool paced{};
  std::array<u64, 6> masks;
  u64 creator_mask{};
  std::vector<u64> samples, idle;
  u64 verified_tasks{}, verified_checksum{};
};
HandoffCase HandoffBenchmark(unsigned samples, unsigned warmup, unsigned tasks,
                             bool paced, bool pin_workers) {
  SemaphorePool pool(pin_workers);
  HandoffCase result{tasks, paced, pool.masks, pool.creator_mask, {}, {}, 0, 0};
  result.samples.reserve(samples);
  result.idle.reserve(samples);
  std::unique_ptr<NativeDisplayClock> cadence;
  if (paced) {
    cadence = std::make_unique<NativeDisplayClock>(0xbecc, 60, 1);
  }
  for (unsigned iteration = 0; iteration < samples + warmup; ++iteration) {
    const auto idle_began = MonotonicMicroseconds();
    if (cadence) {
      Require(cadence->WaitFlipOpportunity(), "Authored cadence stopped");
    }
    const auto began = MonotonicMicroseconds();
    Require(pool.signal(pool.work, static_cast<int>(tasks)) == 0,
            "Cannot dispatch authored batch");
    unsigned timeout = 5000000;
    Require(pool.wait(pool.done, static_cast<int>(tasks), &timeout) == 0 &&
                !pool.failed,
            "Authored completion wait failed");
    const auto elapsed = MonotonicMicroseconds() - began;
    if (iteration >= warmup) {
      result.samples.push_back(elapsed);
      result.idle.push_back(began - idle_began);
    }
  }
  const u64 expected = u64{samples + warmup} * tasks;
  result.verified_tasks = pool.tasks.load();
  result.verified_checksum = pool.checksum.load();
  u64 checksum{};
  for (u64 ticket = 0; ticket < expected; ++ticket) {
    checksum += SemaphorePool::Task(ticket);
  }
  Require(result.verified_tasks == expected &&
              result.verified_checksum == checksum,
          "Authored task count/checksum mismatch");
  pool.Stop();
  return result;
}
void Print(const GpuCase &result, unsigned warmup) {
  std::printf(
      "{\"event\":\"offline_bench_case\",\"name\":\"%s\",\"kind\":\"pm4_gpu\","
      "\"samples\":%zu,\"warmup\":%u,\"draws\":%u,\"prefetches\":%u,"
      "\"acquires\":1,\"acquire_after_draws\":8,\"shader_variants\":2,"
      "\"verified_frames\":%zu,\"first_iteration_us\":%llu}\n",
      result.name, result.samples.size(), warmup, result.draws,
      result.prefetches, result.samples.size() + warmup,
      result.first_iteration_us);
  for (std::size_t index = 0; index < result.samples.size(); ++index) {
    const auto &row = result.samples[index];
    const auto &command = row.command;
    std::printf("{\"event\":\"offline_bench_sample\",\"name\":\"%s\",\"index\":"
                "%zu,\"snapshot_us\":%llu,\"execute_us\":%llu,\"draw_us\":%llu,"
                "\"acquire_us\":%llu,\"prefetch_us\":%llu,\"flush_us\":%llu,"
                "\"other_us\":%llu,\"worker_handoff_us\":%llu,\"completion_"
                "us\":%llu,\"verification_us\":%llu,\"total_us\":%llu}\n",
                result.name, index, row.snapshot, row.execute, command.draw_us,
                command.acquire_us, command.prefetch_us, command.flush_us,
                command.other_us, row.execute - command.duration_us,
                row.completion, row.verification, row.total);
  }
}
void Print(const HandoffCase &result, unsigned warmup) {
  const char *cadence = result.paced ? "cadenced" : "burst";
  std::printf(
      "{\"event\":\"offline_bench_case\",\"name\":\"handoff-%u-%s\",\"kind\":"
      "\"semaphore\",\"samples\":%zu,\"warmup\":%u,\"workers\":6,\"tasks_per_"
      "batch\":%u,\"cadenced\":%s,\"verified_tasks\":%llu,\"verified_"
      "checksum\":%llu,\"native_masks\":[%llu,%llu,%llu,%llu,%llu,%llu],"
      "\"creator_mask\":%llu}\n",
      result.tasks_per_batch, cadence, result.samples.size(), warmup,
      result.tasks_per_batch, result.paced ? "true" : "false",
      result.verified_tasks, result.verified_checksum, result.masks[0],
      result.masks[1], result.masks[2], result.masks[3], result.masks[4],
      result.masks[5], result.creator_mask);
  for (std::size_t index = 0; index < result.samples.size(); ++index) {
    std::printf(
        "{\"event\":\"offline_bench_sample\",\"name\":\"handoff-%u-%s\","
        "\"index\":%zu,\"handoff_us\":%llu,\"cadence_wait_us\":%llu}\n",
        result.tasks_per_batch, cadence, index, result.samples[index],
        result.idle[index]);
  }
}
} // namespace

int RunNativeIndexedDrawSelfTest(const std::filesystem::path& library, bool validation) {
  Watchdog watchdog;
  LogScope log;
  Library api(library);
  NativeResidentGpu gpu(library, validation);
  Allocation allocation;
  Program(allocation.data + 0x10000, true);
  Program(allocation.data + 0x20000, false);
  // v_cvt_f32_u32 v0, v0: the exported position now depends on the actual
  // vertex index. Index 0 draws a center pixel; 65536 is outside the viewport.
  const u32 convert_index = 0x7e000d00;
  std::memcpy(allocation.data + 0x10000 + 8, &convert_index, sizeof(convert_index));
  const u64 shader_hash = 0x1020304050607081ull;
  std::memcpy(allocation.data + 0x10000 + 80, &shader_hash, sizeof(shader_hash));
  const auto layout = api.api.layout();
  const auto display = kt::gfx::DescribeDisplay({0x80002200, 1, 0, 64, 64, 64, 0, 0, 0}, false);
  std::array<std::atomic<u64>, 1> labels{};
  NativeVideoGpuContext target{nullptr, display.layout, {allocation.data, 64 * 64 * 4}, base, labels, &gpu};
  kt::gfx::pm4::Analysis state;
  const std::array<std::vector<u32>, 1> setup{Setup(layout)};
  gpu.Run([&] {
    gpu.Clear();
    // This authored shader has no fragment interpolators. Establish all fixture
    // registers instead of inheriting the game's hardware initialization defaults.
    const std::vector<u32> zero(layout.bank_words);
    gpu.Registers(0, zero);
    gpu.Extent(0, 64, 64);
    (void)ExecuteNativeGpuCommands(target, setup, state, nullptr);
  });
  using DrawIndex = int(BENCH_SYSV *)(u32*, u32, u32, u64, u32, u32);
  const auto draw_index = reinterpret_cast<DrawIndex>(ResolveGnmImport("HlTPoZ-oY7Y#i#G", "libSceGnmDriver", "libSceGnmDriver"));
  Require(draw_index && !ResolveGnmImport("HlTPoZ-oY7Y", "other", "libSceGnmDriver"), "Indexed import resolution failed");
  std::array<u32, 11> commands; commands.fill(73);
  Require(draw_index(commands.data(), 9, 1, base + 0x40000, 0, 1) == -1 &&
          std::all_of(commands.begin(), commands.end(), [](u32 word) { return word == 73; }), "Invalid GNM extent overwrote output");
  for (unsigned iteration = 0; iteration < 4; ++iteration) {
    {
      auto colors = BeginNativeHostWrite(allocation.data, 64 * 64 * 4);
      auto indices = BeginNativeHostWrite(allocation.data + 0x40000, 4);
      Require(bool(colors) && bool(indices), "Could not acquire authored fixture memory");
      std::memset(allocation.data, 0, 64 * 64 * 4);
      const u32 value = iteration < 2 ? 65536u : 0u;
      std::memcpy(allocation.data + 0x40000, &value, 4);
    }
    std::vector<u32> stream;
    Packet(stream, 0x2f, std::array<u32, 1>{1});
    Packet(stream, 0x2a, std::array<u32, 1>{iteration == 0 ? 0u : 1u});
    if (iteration < 3) {
      Require(draw_index(commands.data(), 10, 1, base + 0x40000, 0, 1) == 0 && commands.back() == 73,
              "GNM indexed writer failed or overwrote its tail");
      stream.insert(stream.end(), commands.begin(), commands.begin() + 10);
    } else { Packet(stream, 0x2d, std::array<u32, 2>{1, 2}); }
    const std::array<std::vector<u32>, 1> streams{stream};
    gpu.Run([&] { Require(ExecuteNativeGpuCommands(target, streams, state, nullptr).draws == 1, "Indexed fixture lost a draw"); });
    gpu.Drain();
    std::array<u32, 64 * 64> pixels{};
    gpu.ReadColorForDiagnostics(base, 64, 64, pixels);
    unsigned colored{};
    for (unsigned index = 0; index < pixels.size(); ++index) {
      if (!pixels[index]) continue;
      Require(pixels[index] == 0xff0000ffu && index % 64 >= 31 && index % 64 <= 32 && index / 64 >= 31 && index / 64 <= 32,
              "Indexed fixture produced unexpected pixels");
      ++colored;
    }
    Require(colored == (iteration == 1 ? 0u : 1u), "Indexed format/address/content did not control the actual GPU pixels");
    std::printf("{\"event\":\"indexed_draw_pixel_case\",\"case\":%u,\"colored\":%u,\"passed\":true}\n", iteration, colored);
  }
  unsigned rejected{};
  for (const auto descriptor : {std::array<u64, 3>{0, 1, 0}, {base + 1, 1, 0}, {base + 2, 1, 1},
                               {base, 1, 2}, {0xfffffffffeull, 2, 0}}) {
    try { gpu.DrawIndexed(descriptor[0], static_cast<unsigned>(descriptor[1]), static_cast<unsigned>(descriptor[1]), 1, static_cast<unsigned>(descriptor[2])); }
    catch (const std::exception&) { ++rejected; }
  }
  const auto stats = api.api.stats();
  Require(rejected == 5 && stats.draws == 4 && !stats.validation_errors, "Indexed descriptor validation or GPU validation failed");
  log.Stop();
  std::printf("{\"event\":\"indexed_draw_self_test\",\"passed\":true,\"draws\":4,\"rejected\":5,\"validation\":%s,\"validationErrors\":0}\n", validation ? "true" : "false");
  return 0;
}

int RunNativeOfflineBench(const std::filesystem::path &library,
                          unsigned samples, unsigned warmup, bool pin_workers) {
  Require(samples >= 1 && samples <= 10000 && warmup <= 1000,
          "Offline benchmark count out of range");
  Watchdog watchdog;
  LogScope log;
  // The two synchronization batches match observed 14/16-credit patterns;
  // They run production semaphores. Default retains the host eligible set;
  // the explicit pinned control reproduces the retired hard map here only.
  const auto handoff14 = HandoffBenchmark(samples, warmup, 14, false, pin_workers);
  const auto handoff16 = HandoffBenchmark(samples, warmup, 16, false, pin_workers);
  const auto paced14 = HandoffBenchmark(samples, warmup, 14, true, pin_workers);
  const auto paced16 = HandoffBenchmark(samples, warmup, 16, true, pin_workers);
  std::vector<GpuCase> cases;
  {
    Library api(library);
    // Performance samples exclude optional Vulkan validation instrumentation.
    // Game defaults are unchanged; RGB readback/counters remain mandatory.
    NativeResidentGpu gpu(library, false);
    Allocation allocation;
    Program(allocation.data + 0x10000, true);
    Program(allocation.data + 0x20000, false);
    Program(allocation.data + 0x30000, false, true);
    const auto layout = api.api.layout();
    Require(layout.bank_words == 0xd000,
            "Offline fixture needs the supported register bank");
    cases.push_back(GpuBenchmark(gpu, layout, 170, 60, "pm4-170-60", samples,
                                 warmup, allocation));
    cases.push_back(GpuBenchmark(gpu, layout, 250, 86, "pm4-250-86", samples,
                                 warmup, allocation));
    const auto stats = api.api.stats();
    Require(stats.validation_errors == 0 && stats.dispatches == 0 &&
                stats.draws == u64{samples + warmup} * (170 + 250),
            "GPU validation or executed-draw count differs");
  }
  log.Stop();
  PrintNativeAsyncLogDiagnostics();
  std::printf("{\"event\":\"offline_bench_begin\",\"schema\":1,\"gpu_api\":%u,"
              "\"samples\":%u,\"warmup\":%u,\"game_execution\":false,\"window_"
              "created\":false,\"authored_only\":true,\"validation\":false,"
              "\"semaphore_wait\":\"%s\",\"worker_affinity\":\"%s\",\"process_priority_"
              "class\":%lu}\n",
              RuntimeApiVersion, samples, warmup,
              NativeSemaphoreWaitName(), pin_workers ? "pinned" : "inherited",
              GetPriorityClass(GetCurrentProcess()));
  Print(handoff14, warmup);
  Print(handoff16, warmup);
  Print(paced14, warmup);
  Print(paced16, warmup);
  for (const auto &result : cases) {
    Print(result, warmup);
  }
  std::puts("{\"event\":\"offline_bench_complete\",\"schema\":1,\"passed\":"
            "true,\"cases\":6}");
  return 0;
}
