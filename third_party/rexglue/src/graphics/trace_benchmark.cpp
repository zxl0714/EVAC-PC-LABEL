// Replays a private capture through the production D3D12 command processor.
// No guest code, simulation, game clock or host swapchain is benchmarked here.
#include <chrono>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <rex/cvar.h>
#include <rex/graphics/d3d12/graphics_system.h>
#include <rex/graphics/trace_player.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <xxhash.h>

REXCVAR_DEFINE_STRING(bench_trace, "", "GPU benchmark", "Local .xtr capture").transient();
REXCVAR_DEFINE_STRING(bench_output, "", "GPU benchmark", "New output directory").transient();
REXCVAR_DEFINE_STRING(bench_reference, "", "GPU benchmark", "Original frame PPM to verify").transient();
REXCVAR_DEFINE_BOOL(bench_reuse_memory, false, "GPU benchmark", "Retain verified unchanged CPU texture data").transient();
REXCVAR_DEFINE_BOOL(bench_alternate_resolve, false, "GPU benchmark", "Compare old/new/new/old within one process").transient();
REXCVAR_DEFINE_BOOL(bench_alternate_overwrite, false, "GPU benchmark", "Compare old/full-overwrite uploads in ABBA order").transient();
REXCVAR_DEFINE_INT32(bench_warmup, 20, "GPU benchmark", "Warmup replay count").range(1, 1000).transient();
REXCVAR_DEFINE_INT32(bench_iterations, 120, "GPU benchmark", "Measured replay count").range(1, 10000).transient();
REXCVAR_DECLARE(int32_t, d3d12_bench_resolve_repetitions);
REXCVAR_DECLARE(bool, d3d12_native_rgba8_resolve);
REXCVAR_DECLARE(bool, d3d12_resolve_skip_overwritten_uploads);
REXCVAR_DECLARE(bool, d3d12_profile_cpu_work);

namespace {
uint64_t FileHash(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("trace hash read failed");
  auto* state = XXH3_createState();
  if (!state) throw std::runtime_error("hash allocation failed");
  XXH3_64bits_reset(state);
  std::array<char, 65536> buffer;
  while (stream.read(buffer.data(), buffer.size()) || stream.gcount())
    XXH3_64bits_update(state, buffer.data(), size_t(stream.gcount()));
  const auto hash = XXH3_64bits_digest(state);
  XXH3_freeState(state);
  if (!stream.eof()) throw std::runtime_error("trace hash read failed");
  return hash;
}

uint64_t ImageHash(const rex::ui::RawImage& image) {
  auto* state = XXH3_createState();
  if (!state) throw std::runtime_error("hash allocation failed");
  XXH3_64bits_reset(state);
  XXH3_64bits_update(state, &image.width, sizeof(image.width));
  XXH3_64bits_update(state, &image.height, sizeof(image.height));
  for (uint32_t y = 0; y < image.height; ++y) {
    const auto* row = image.data.data() + y * image.stride;
    // CaptureGuestOutput's X byte is undefined. Compare RGB, not row padding.
    for (uint32_t x = 0; x < image.width; ++x) XXH3_64bits_update(state, row + 4 * x, 3);
  }
  const auto hash = XXH3_64bits_digest(state);
  XXH3_freeState(state);
  return hash;
}

void WriteImage(const std::filesystem::path& path, const rex::ui::RawImage& image) {
  std::ofstream stream(path, std::ios::binary);
  stream << "P6\n" << image.width << " " << image.height << "\n255\n";
  for (uint32_t y = 0; y < image.height; ++y) {
    for (uint32_t x = 0; x < image.width; ++x) {
      stream.write(reinterpret_cast<const char*>(image.data.data() + y * image.stride + 4 * x), 3);
    }
  }
  if (!stream) throw std::runtime_error("image write failed");
}

int Run() {
  if (REXCVAR_GET(bench_alternate_resolve) && REXCVAR_GET(bench_alternate_overwrite)) {
    std::fprintf(stderr, "Alternate only one optimization per benchmark\n");
    return 2;
  }
  const auto trace = std::filesystem::u8path(REXCVAR_GET(bench_trace));
  const auto output = std::filesystem::u8path(REXCVAR_GET(bench_output));
  if (!std::filesystem::is_regular_file(trace) || output.empty() ||
      std::filesystem::exists(output)) {
    std::fprintf(stderr, "Use --bench_trace=<local.xtr> --bench_output=<new-directory>\n");
    return 2;
  }
  std::filesystem::create_directories(output);
  auto log = rex::BuildLogConfig((output / "gpu.log").string().c_str(), "info", {});
  log.log_to_console = false;
  rex::InitLogging(log);
  // Synchronous compilation prevents an incomplete warmup image being accepted.
  rex::cvar::SetFlagByName("async_shader_compilation", "false");
  rex::cvar::SetFlagByName("d3d12_profile_submissions", "true");
  rex::cvar::SetFlagByName("d3d12_profile_cpu_interval", "1");
  rex::cvar::FinalizeInit();

  rex::Runtime runtime(output);
  auto graphics = std::make_unique<rex::graphics::d3d12::D3D12GraphicsSystem>();
  auto* gpu = graphics.get();
  // A real offscreen presenter keeps resolve/gamma/output work enabled.
  if (XFAILED(gpu->SetupPresentation(nullptr))) return 3;
  rex::RuntimeConfig config;
  config.graphics = std::move(graphics);
  if (XFAILED(runtime.Setup(std::move(config)))) return 3;
  rex::graphics::TracePlayer player(gpu);
  if (!player.Open(REXCVAR_GET(bench_trace)) || player.frame_count() < 1) return 4;
  player.set_reuse_memory(REXCVAR_GET(bench_reuse_memory));
  const int warmup = REXCVAR_GET(bench_warmup);
  const int iterations = REXCVAR_GET(bench_iterations);
  std::ofstream settings(output / "settings.json");
  settings << "{\"trace_xxh3\":\"" << std::hex << FileHash(trace) << std::dec
           << "\",\"trace_bytes\":" << std::filesystem::file_size(trace)
           << ",\"copy_repetitions\":" << REXCVAR_GET(d3d12_bench_resolve_repetitions)
           << ",\"native_rgba8\":" << std::boolalpha << REXCVAR_GET(d3d12_native_rgba8_resolve)
           << ",\"reuse_memory\":" << REXCVAR_GET(bench_reuse_memory)
           << ",\"alternate_resolve\":" << REXCVAR_GET(bench_alternate_resolve)
           << ",\"alternate_overwrite\":" << REXCVAR_GET(bench_alternate_overwrite)
           << ",\"overwrite_upload\":" << REXCVAR_GET(d3d12_resolve_skip_overwritten_uploads)
           << ",\"profile_cpu_work\":" << REXCVAR_GET(d3d12_profile_cpu_work)
           << ",\"warmup\":" << warmup << ",\"iterations\":" << iterations << "}\n";
  settings.close();
  if (!settings) return 8;
  std::ofstream csv(output / "replays.csv");
  csv << "iteration,phase,replay_wall_us,capture_wall_us,rgb_hash,width,height,restore_us,packets_us,primary_end_us,restored_bytes,packets,reused_bytes,native_rgba8,overwrite_upload\n";
  uint64_t reference = 0;
  for (int i = 0; i < warmup + iterations; ++i) {
    // The preceding CaptureGuestOutput has completed the frame. ABBA ordering
    // reduces drift between separate runs without changing system GPU clocks.
    if (REXCVAR_GET(bench_alternate_resolve) && !rex::cvar::SetFlagByName(
        "d3d12_native_rgba8_resolve", (i % 4 == 1 || i % 4 == 2) ? "true" : "false")) return 10;
    if (REXCVAR_GET(bench_alternate_overwrite) && !rex::cvar::SetFlagByName(
        "d3d12_resolve_skip_overwritten_uploads", (i % 4 == 1 || i % 4 == 2) ? "true" : "false")) return 10;
    const auto begin = std::chrono::steady_clock::now();
    if (!player.ReplayFrame(0) || !player.WaitOnPlayback(std::chrono::seconds(15))) return 5;
    const auto replayed = std::chrono::steady_clock::now();
    rex::ui::RawImage image;
    if (!gpu->presenter()->CaptureGuestOutput(image) || !image.width || !image.height) return 6;
    const auto captured = std::chrono::steady_clock::now();
    const auto hash = ImageHash(image);
    if (i == 0) {
      reference = hash;
      WriteImage(output / "frame.ppm", image);
      if (!REXCVAR_GET(bench_reference).empty()) {
        std::ifstream expected(std::filesystem::u8path(REXCVAR_GET(bench_reference)), std::ios::binary);
        std::ifstream actual(output / "frame.ppm", std::ios::binary);
        const std::string expected_bytes((std::istreambuf_iterator<char>(expected)), {});
        const std::string actual_bytes((std::istreambuf_iterator<char>(actual)), {});
        if (!expected || expected_bytes.empty() || expected_bytes != actual_bytes) {
          std::fprintf(stderr, "Replay differs from original captured output\n");
          return 9;
        }
      }
    } else if (hash != reference) {
      WriteImage(output / "mismatch.ppm", image);
      std::fprintf(stderr, "Replay image changed at iteration %d\n", i);
      return 7;
    }
    csv << i << ',' << (i < warmup ? "warmup" : "measured") << ','
        << std::chrono::duration<double, std::micro>(replayed - begin).count() << ','
        << std::chrono::duration<double, std::micro>(captured - replayed).count() << ','
        << std::hex << hash << std::dec << ',' << image.width << ',' << image.height << ','
        << player.replay_stats().restore_us << ',' << player.replay_stats().packets_us << ','
        << player.replay_stats().primary_end_us << ',' << player.replay_stats().restored_bytes
        << ',' << player.replay_stats().packets << ',' << player.replay_stats().reused_bytes
        << ',' << int(REXCVAR_GET(d3d12_native_rgba8_resolve))
        << ',' << int(REXCVAR_GET(d3d12_resolve_skip_overwritten_uploads)) << '\n';
    REXGPU_INFO("GPU_BENCH iteration={} phase={} rgb_hash={:016X}", i,
                i < warmup ? "warmup" : "measured", hash);
  }
  csv.close();
  if (!csv) return 8;
  std::printf("Replayed %d + %d frames, RGB hash %016llX\n", warmup, iterations,
              static_cast<unsigned long long>(reference));
  return 0;
}
}  // namespace

namespace rex::graphics {
bool CaptureTraceReference(ui::Presenter* presenter, const std::filesystem::path& path) {
  try {
    ui::RawImage image;
    if (!presenter->CaptureGuestOutput(image)) return false;
    WriteImage(path, image);
    REXGPU_INFO("GPU_TRACE reference={} rgb_hash={:016X}", path.string(), ImageHash(image));
    return true;
  } catch (const std::exception& error) {
    REXGPU_ERROR("GPU_TRACE reference failed: {}", error.what());
    return false;
  }
}
}  // namespace rex::graphics

extern "C" REX_GPU_PLUGIN_EXPORT int rex_gpu_trace_benchmark(int argc, char** argv) {
  rex::InitLoggingEarly();
  try {
    rex::cvar::Init(argc, argv);
    return Run();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "GPU trace benchmark: %s\n", error.what());
    return 1;
  }
}
