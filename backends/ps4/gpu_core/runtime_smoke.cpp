#include "gpu_worker.h"
#include "graphics/display_layout.h"
#include "native_memory_backend.h"
#include "present_window.h"
#include "runtime_api.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

unsigned LegacyCompilerCanaryCalls();
namespace {
using namespace Ketsui::GpuCore;
using u64 = std::uint64_t;
using u32 = std::uint32_t;
void Require(bool ok, const char *message) {
  if (!ok) {
    throw std::runtime_error(message);
  }
}
class Watchdog {
  unsigned seconds;
  std::mutex mutex;
  std::condition_variable_any wake;
  std::jthread thread{[this](std::stop_token stop) {
    std::unique_lock lock{mutex};
    wake.wait_for(lock, stop, std::chrono::seconds(seconds),
                  [] { return false; });
    if (!stop.stop_requested()) {
      std::fputs("gpu_runtime_smoke: timeout\n", stderr);
      std::abort();
    }
  }};

public:
  explicit Watchdog(unsigned limit) : seconds{limit} {}
  ~Watchdog() { thread.request_stop(); }
};
struct Library {
  HMODULE module{};
  RuntimeApi api{};
  Library() {
    std::array<wchar_t, 32768> name;
    const auto length = GetModuleFileNameW(nullptr, name.data(),
                                           static_cast<DWORD>(name.size()));
    Require(length && length < name.size(),
            "Could not locate runtime smoke executable");
    const auto path = std::filesystem::path{name.data()}.parent_path() /
                      "KetsuiGpuRuntime.dll";
    module = LoadLibraryExW(path.c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
    Require(module != nullptr, "Could not load private GPU runtime DLL");
    const auto get = reinterpret_cast<GetRuntimeApi>(
        GetProcAddress(module, "KetsuiGpuRuntimeGetApi"));
    Require(get && !get(RuntimeApiVersion + 1, sizeof(api), &api) &&
                !get(RuntimeApiVersion, sizeof(api) - 1, &api) &&
                get(RuntimeApiVersion, sizeof(api), &api),
            "Runtime API negotiation failed");
  }
  ~Library() {
    if (module) {
      FreeLibrary(module);
    }
  }
};
struct NativeSession {
  const RuntimeApi &api;
  GpuWorker worker;
  void *context{};
  std::map<u64, NativeMemoryRangeInfo> mappings;
  NativeMemoryCacheBackend memory{Hooks()};
  void Call(bool ok, const RuntimeError &error) { Require(ok, error.message); }
  NativeCacheHooks Hooks() {
    return {.sync = [this](auto task) { worker.Sync(std::move(task)); },
            .is_gpu_thread = [this] { return worker.IsCurrent(); },
            .mapped =
                [this](const auto &range) {
                  RuntimeError error;
                  Call(api.map(context, range.address, range.bytes, &error),
                       error);
                  mappings[range.mapping_address] = range;
                },
            .before_change = [this](auto ranges) { BeforeChange(ranges); },
            .access =
                [this](auto ranges, bool write) {
                  for (const auto &range : ranges) {
                    RuntimeError error;
                    Call(api.access(context, range.address, range.bytes, write,
                                    &error),
                         error);
                  }
                },
            .drain =
                [this] {
                  std::vector<NativeMemoryRangeInfo> all;
                  for (const auto &[base, range] : mappings) {
                    (void)base;
                    all.push_back(range);
                  }
                  BeforeChange(all);
                  RuntimeError error;
                  Call(api.drain(context, &error), error);
                }};
  }
  void BeforeChange(std::span<const NativeMemoryRangeInfo> ranges) {
    for (const auto &range : ranges) {
      RuntimeError error;
      for (const auto &alias :
           NativeMemoryAliases(range.address, range.bytes)) {
        Call(api.access(context, alias.address, alias.bytes, true, &error),
             error);
      }
      Call(api.unmap(context, range.address, range.bytes, &error), error);
      mappings.erase(range.mapping_address);
    }
  }
  NativeSession(const RuntimeApi &api_, bool validation, void *window = nullptr, bool vsync = true)
      : api{api_} {
    const HostSettings settings{.validation_core = validation,
                                .validation_sync = validation,
                                .log_directory =
                                    std::filesystem::current_path(),
                                .presentation_support = window != nullptr,
                                .vsync = vsync};
    worker.Sync([&] {
      RuntimeError error;
      Call(api.create(&settings, &memory, &context, &error), error);
    });
    try {
      memory.Attach();
    } catch (...) {
      worker.Sync([&] {
        RuntimeError error;
        Call(api.destroy(context, &error), error);
        context = nullptr;
      });
      throw;
    }
  }
  ~NativeSession() {
    try {
      memory.Detach();
      worker.Sync([&] {
        RuntimeError error;
        Call(api.destroy(context, &error), error);
        context = nullptr;
      });
    } catch (const std::exception &error) {
      std::fprintf(stderr, "GPU teardown failed: %s\n", error.what());
      std::terminate();
    }
  }
  void Write(u32 offset, std::span<const u32> words) {
    RuntimeError error;
    Call(api.registers(context, offset, words.data(),
                       static_cast<u32>(words.size()), &error),
         error);
  }
  void Word(u32 offset, u32 value) { Write(offset, {&value, 1}); }
};
constexpr u64 base = 0x4000000000, allocation_bytes = 0x100000;
struct Allocation {
  using AllocateFn = int(__attribute__((sysv_abi)) *)(std::int64_t,
                                                      std::int64_t, u64, u64,
                                                      int, std::int64_t *);
  using MapFn = int(__attribute__((sysv_abi)) *)(void **, u64, int, int,
                                                 std::int64_t, u64);
  using ReleaseFn = int(__attribute__((sysv_abi)) *)(u64, u64);
  std::int64_t physical{-1};
  unsigned char *data{};
  Allocation() {
    const auto allocate =
        reinterpret_cast<AllocateFn>(ResolveMemoryImport("rTXw65xmLIA"));
    const auto map =
        reinterpret_cast<MapFn>(ResolveMemoryImport("L-Q3LEjIbgA"));
    Require(allocate(0, 0x100000000ull, allocation_bytes, 0x10000, 3,
                     &physical) == 0,
            "Draw backing allocation failed");
    void *view = reinterpret_cast<void *>(base);
    Require(map(&view, allocation_bytes, 0x33, 0x90, physical, 0) == 0 &&
                view == reinterpret_cast<void *>(base),
            "Draw backing map failed");
    data = static_cast<unsigned char *>(view);
    std::memset(data, 0, allocation_bytes);
  }
  ~Allocation() {
    const auto release =
        reinterpret_cast<ReleaseFn>(ResolveMemoryImport("MBuItvba6z8"));
    if (physical >= 0 &&
        release(static_cast<u64>(physical), allocation_bytes)) {
      std::terminate();
    }
  }
};
void Program(unsigned char *destination, bool vertex, bool green = false) {
  // Authored GCN exports a center point plus its size, or a solid RGBA color.
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
  std::memcpy(blob.data() + 72, &info, 4);
  const u64 hash = vertex  ? 0x1020304050607080ull
                   : green ? 0x8172635445362718ull
                           : 0x123456789abcdef0ull;
  std::memcpy(blob.data() + 80, &hash, 8);
  std::memcpy(destination, blob.data(), blob.size());
}
void Configure(NativeSession &session, const RuntimeRegisterLayout &layout) {
  std::vector<u32> zero(layout.bank_words);
  session.Write(0, zero);
  const auto shader = [&](u32 offset, u64 address) {
    const std::array<u32, 4> words{static_cast<u32>(address >> 8),
                                   static_cast<u32>(address >> 40), 0, 0};
    session.Write(offset, words);
  };
  shader(layout.vs_program, base + 0x10000);
  shader(layout.ps_program, base + 0x20000);
  const std::array<u32, 6> color{
      static_cast<u32>(base >> 8), 7, 63, 0, 0x8028, 8 | (8 << 5)};
  session.Write(layout.color_buffers, color);
  session.Word(layout.color_target_mask, 15);
  session.Word(layout.color_shader_mask, 15);
  session.Word(layout.color_control, (0xcc << 16) | (1 << 4));
  session.Word(layout.color_export_format, 9);
  session.Word(layout.shader_pos_format, 4 | (1 << 4));
  session.Word(layout.vs_output_control, (1 << 16) | (1 << 21));
  session.Word(layout.primitive_type, 1);
  session.Word(layout.num_indices, 1);
  session.Word(layout.num_instances, 1);
  const auto f = [](float value) { return std::bit_cast<u32>(value); };
  const std::array<u32, 6> viewport{f(32), f(32),   f(32),
                                    f(32), f(0.5f), f(0.5f)};
  session.Write(layout.viewports, viewport);
  session.Word(layout.viewport_control, 0x3f);
  const std::array<u32, 2> scissor{0, 64 | (64 << 16)}, depth{f(0), f(1)};
  session.Write(layout.screen_scissor, scissor);
  session.Write(layout.window_scissor, scissor);
  session.Write(layout.generic_scissor, scissor);
  session.Write(layout.viewport_depths, depth);
  RuntimeError error;
  session.Call(session.api.extent(session.context, 0, 64, 64, &error), error);
}
unsigned CheckPixels(NativeSession &session, bool green) {
  std::array<u32, 64 * 64> pixels{};
  RuntimeError error;
  session.Call(session.api.read_color(session.context, base, 64, 64,
                                      pixels.data(), sizeof(pixels), &error),
               error);
  unsigned colored = 0;
  for (unsigned y = 0; y < 64; ++y) {
    for (unsigned x = 0; x < 64; ++x) {
      const auto value = pixels[y * 64 + x];
      if (!value) {
        continue;
      }
      if (value != (green ? 0xff00ff00u : 0xff0000ffu) || x < 31 || x > 32 ||
          y < 31 || y > 32) {
        std::fprintf(stderr, "Unexpected pixel (%u,%u)=%08x green=%d\n", x, y,
                     value, green);
        throw std::runtime_error("Actual GPU pixel output is incorrect");
      }
      ++colored;
    }
  }
  Require(colored == 1,
          "Authored GCN did not produce exactly one visible pixel");
  return colored;
}
} // namespace
int main(int argc, char **argv) {
  try {
    bool validation = false, presentation = false, interactive = false, vsync = true;
    for (int i = 1; i < argc; ++i) {
      const std::string_view argument{argv[i]};
      if (argument == "--validation") {
        validation = true;
      } else if (argument == "--presentation") {
        presentation = true;
      } else if (argument == "--interactive") {
        interactive = true;
      } else if (argument == "--no-vsync") {
        vsync = false;
      } else {
        throw std::runtime_error("Unknown runtime smoke argument");
      }
    }
    Require(!interactive || presentation,
            "Interactive smoke requires --presentation");
    Watchdog watchdog{interactive ? 60u : 25u};
    std::unique_ptr<PresentWindow> window;
    if (presentation) {
      Require(SetProcessDpiAwarenessContext(
                  DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2),
              "Presentation DPI setup failed");
      window = std::make_unique<PresentWindow>();
    }
    Library library;
    const auto layout = library.api.layout();
    Require(layout.bank_words == 0xd000 && layout.vs_program == 0x2c48 &&
                layout.ps_program == 0x2c08,
            "Runtime register layout does not match the command stream ABI");
    unsigned pixels = 0;
    u64 first_tick = 0, last_tick = 0, presents = 0;
    unsigned zero_extents = 0;
    bool resized = false, tiled_presented = false, reopened = false;
    u64 resident_read_bytes = 0, resident_write_bytes = 0;
    {
      NativeSession session{library.api, validation,
                            window ? window->Handle() : nullptr, vsync};
      Allocation allocation;
      if (window) {
        auto write_lease =
            BeginNativeHostWrite(allocation.data + 0x30000, 0x1000);
        Require(bool(write_lease),
                "Could not pin native memory before late window binding");
        session.worker.Sync([&] {
          RuntimeError error;
          session.Call(
              library.api.set_window(session.context, window->Handle(), &error),
              error);
        });
      }
      Program(allocation.data + 0x10000, true);
      Program(allocation.data + 0x20000, false);
      RuntimeError error;
      Require(!library.api.draw(session.context, false, 0, &error) &&
                  std::strstr(error.message, "owning worker"),
              "Wrong-thread GPU call was accepted");
      session.worker.Sync([&] {
        Require(!library.api.registers(session.context, layout.bank_words,
                                       &layout.bank_words, 1, &error),
                "Register overflow was accepted");
        Configure(session, layout);
        for (unsigned i = 0; i < 64; ++i) {
          session.Call(library.api.draw(session.context, false, 0, &error),
                       error);
        }
        session.Call(library.api.flush(session.context, &first_tick, &error),
                     error);
        pixels += CheckPixels(session, false);
      });
      const u32 truncated_length = (4u << 8) | 1u;
      std::memcpy(allocation.data + 0x20000 + 72, &truncated_length, 4);
      session.worker.Sync([&] {
        Require(!library.api.draw(session.context, false, 0, &error) &&
                    std::strstr(error.message, "code extent"),
                "Truncated shader declaration reached the decoder");
      });
      Program(allocation.data + 0x20000, false, true);
      session.worker.Sync([&] {
        session.Call(library.api.draw(session.context, false, 0, &error),
                     error);
        session.Call(library.api.flush(session.context, &last_tick, &error),
                     error);
        pixels += CheckPixels(session, true);
      });
      if (presentation) {
        const std::array<u32, 4> colors{0xff0000ff, 0xff00ff00, 0xffff0000,
                                        0xffffffff};
        std::array<u32, 64 * 64> pattern;
        for (unsigned y = 0; y < 64; ++y) {
          for (unsigned x = 0; x < 64; ++x) {
            pattern[y * 64 + x] = colors[(y / 32) * 2 + x / 32];
          }
        }
        std::memcpy(allocation.data, pattern.data(), sizeof(pattern));
        RuntimeDisplay display{base, 64, 64, 64, 0x80002200, 1};
        const auto present = [&](const kt::diagnostics::PresentationPacing* pacing = nullptr) {
          RuntimePresentResult result;
          for (unsigned retry = 0; retry < 9; ++retry) {
            u64 paced_draw_tick{};
            session.worker.Sync([&] {
              // Exercise the native submit-before-pacing path. This does not
              // report a flip; the following final present must still wait for
              // its own completion and advance past the earlier queue tick.
              session.Call(library.api.flush(session.context, &paced_draw_tick, &error), error);
              session.Call(library.api.prepare_present(session.context, &display, &result, &error),
                           error);
              if (result.status == RuntimePresentStatus::Prepared) {
                Require(!result.timing.completed && result.tick > paced_draw_tick,
                        "Prepare fabricated a completed present or lost queue order");
              }
            });
            if (result.status == RuntimePresentStatus::Prepared) {
              const bool accepted = !pacing || pacing->wait(pacing->context);
              session.worker.Sync([&] { session.Call(library.api.complete_present(session.context, accepted, &result, &error), error); });
            }
            const auto& t = result.timing;
            if (result.status == RuntimePresentStatus::Completed) {
              Require(t.completed && t.duration_us == t.prepare_us+t.retire_us+t.acquire_us+t.record_us+
                  t.submit_us+t.queue_present_us+t.gpu_wait_us+t.present_wait_us+t.finalize_us,
                  "Resident presentation wall-time scopes do not reconcile");
              Require(result.tick > paced_draw_tick, "Early draw flush fabricated final presentation completion");
            } else {
              Require(!t.completed, "Incomplete presentation fabricated completed timing");
            }
            if (result.status != RuntimePresentStatus::OutOfDate) {
              return result;
            }
          }
          throw std::runtime_error(
              "Presentation did not recover from OUT_OF_DATE");
        };
        auto result = present();
        Require(result.status == RuntimePresentStatus::Completed &&
                    result.tick > last_tick && result.completed_presents == 1,
                "First resident presentation did not complete");
        struct PacingFixture { NativeSession* session; unsigned calls{}; bool accept = true; bool caller_thread = true; bool memory_serviced{}; } paced{&session};
        const kt::diagnostics::PresentationPacing pacing{&paced, [](void* opaque) {
          auto& state = *static_cast<PacingFixture*>(opaque);
          ++state.calls; state.caller_thread &= !state.session->worker.IsCurrent();
          // Coherent memory service must remain available while the caller
          // owns a prepared frame and waits for the normal pacing deadline.
          state.session->worker.Sync([&] {
            RuntimeError error;
            state.session->Call(state.session->api.access(state.session->context, base, 4, false, &error), error);
            state.memory_serviced = true;
          });
          Sleep(5);
          return state.accept;
        }};
        const auto paced_before = result.completed_presents;
        result = present(&pacing);
        Require(result.status == RuntimePresentStatus::Completed && result.completed_presents == paced_before+1 &&
                    paced.calls && paced.caller_thread && paced.memory_serviced && result.timing.between_phases_us >= 4000,
                "Submitted composite did not retain its host pacing callback and completed fence");
        const auto before_cancel = result.completed_presents;
        paced.accept = false;
        result = present(&pacing);
        Require(result.status == RuntimePresentStatus::Cancelled && !result.timing.completed && !result.tick &&
                    result.completed_presents == before_cancel,
                "Cancelled pacing published a guest completion or leaked the acquired frame");
        result = present();
        Require(result.status == RuntimePresentStatus::Completed && result.completed_presents == before_cancel+1 &&
                    result.timing.completed,
                "Presentation did not recover after retiring a cancelled paced frame");
        const auto old_width = result.width, old_height = result.height;
        window->Resize(480, 320);
        result = present();
        Require(result.status == RuntimePresentStatus::Completed &&
                    (result.width != old_width || result.height != old_height),
                "Resize did not recreate the swapchain");
        resized = true;
        const auto before_minimize = result.completed_presents;
        window->Minimize();
        result = present();
        Require(result.status == RuntimePresentStatus::ZeroExtent &&
                    result.completed_presents == before_minimize &&
                    !result.tick,
                "Minimized presentation reported a completed flip");
        ++zero_extents;
        window->Restore();
        result = present();
        Require(result.status == RuntimePresentStatus::Completed &&
                    result.completed_presents == before_minimize + 1,
                "Restore did not complete the retained frame");
        const auto tiled = kt::gfx::DescribeDisplay(
            {0x80000000, 0, 0, 128, 64, 128, 0, 0, 0}, false);
        Require(tiled.error == kt::gfx::LayoutError::None &&
                    tiled.layout.bytes == 32768,
                "Tiled display fixture layout is invalid");
        std::vector<std::uint8_t> tiled_pixels(128 * 64 * 4),
            tiled_backing(tiled.layout.bytes);
        for (unsigned y = 0; y < 64; ++y) {
          for (unsigned x = 0; x < 128; ++x) {
            auto color = colors[(y / 32) * 2 + x / 64];
            color = (color & 0xff00ff00) | ((color & 0xff) << 16) |
                    ((color >> 16) & 0xff);
            std::memcpy(tiled_pixels.data() + (y * 128 + x) * 4, &color, 4);
          }
        }
        kt::gfx::WriteDisplayPixels(tiled.layout, tiled_pixels, tiled_backing);
        std::memcpy(allocation.data + 0x40000, tiled_backing.data(),
                    tiled_backing.size());
        display = {base + 0x40000, 128, 64, 128, 0x80000000, 0};
        result = present();
        Require(result.status == RuntimePresentStatus::Completed,
                "GPU-tiled BGRA presentation failed");
        tiled_presented = true;
        const auto traffic_before = session.memory.Traffic();
        const auto end = std::chrono::steady_clock::now() +
                         std::chrono::seconds(interactive ? 45 : 0);
        for (unsigned frame = 0;
             frame < 8 ||
             (interactive && std::chrono::steady_clock::now() < end &&
              !window->Closed());
             ++frame) {
          result = present();
          if (result.status == RuntimePresentStatus::ZeroExtent) {
            ++zero_extents;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
          }
        }
        const auto traffic_after = session.memory.Traffic();
        resident_read_bytes =
            traffic_after.read_bytes - traffic_before.read_bytes;
        resident_write_bytes =
            traffic_after.write_bytes - traffic_before.write_bytes;
        Require(!resident_read_bytes && !resident_write_bytes,
                "Unchanged resident presentation copied native backing");
        presents = result.completed_presents;
        Require(presents >= 12,
                "Presentation loop did not complete its expected frames");
        const auto previous_tick = result.tick;
        session.worker.Sync([&] {
          session.Call(library.api.set_window(session.context, nullptr, &error),
                       error);
          Require(!library.api.present(session.context, &display, &result,
                                       &error) &&
                      std::strstr(error.message, "no presentation window"),
                  "Unbound runtime accepted a flip");
          session.Call(
              library.api.set_window(session.context, window->Handle(), &error),
              error);
        });
        result = present();
        Require(result.status == RuntimePresentStatus::Completed &&
                    result.completed_presents == 1 &&
                    result.tick > previous_tick,
                "Window reopen lost the GPU session or completion order");
        ++presents;
        reopened = true;
      }
    }
    const auto stats = library.api.stats();
    Require(stats.draws == 65 && stats.dispatches == 0 &&
                !stats.validation_errors && last_tick > first_tick &&
                !LegacyCompilerCanaryCalls(),
            "GPU draw counts, completion ticks, validation or symbol isolation "
            "failed");
    const auto written = std::printf(
        "{\"event\":\"gpu_runtime_smoke\",\"passed\":true,\"validation\":%s,"
        "\"draws\":%llu,\"verifiedPixels\":%u,\"firstTick\":%llu,\"lastTick\":%"
        "llu,\"canaryCalls\":%u,\"validationErrors\":%u,\"presentation\":%s,"
        "\"presents\":%llu,\"zeroExtents\":%u,\"resized\":%s,"
        "\"tiledPresented\":%s,\"residentReadBytes\":%llu,"
        "\"residentWriteBytes\":%llu,\"reopened\":%s}\n",
        validation ? "true" : "false",
        static_cast<unsigned long long>(stats.draws), pixels,
        static_cast<unsigned long long>(first_tick),
        static_cast<unsigned long long>(last_tick), LegacyCompilerCanaryCalls(),
        stats.validation_errors, presentation ? "true" : "false",
        static_cast<unsigned long long>(presents), zero_extents,
        resized ? "true" : "false", tiled_presented ? "true" : "false",
        static_cast<unsigned long long>(resident_read_bytes),
        static_cast<unsigned long long>(resident_write_bytes),
        reopened ? "true" : "false");
    Require(written > 0 && std::fflush(stdout) == 0,
            "Could not flush the runtime test result");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "gpu_runtime_smoke: %s\n", error.what());
    return 1;
  }
}
