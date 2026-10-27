// Developer-only CPU attribution. Never compiled into player builds.
#pragma once
#if defined(REX_GPU_TRACE_BENCH)
#include <array>
#include <chrono>
#include <windows.h>
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DECLARE(bool, d3d12_profile_cpu_work);
REXCVAR_DECLARE(int32_t, d3d12_profile_cpu_interval);
namespace rex::graphics::d3d12::work_profile {
enum class Phase { Setup, Primitive, RenderTargets, Pipeline, Textures, FixedState,
                   Bindings, Residency, Draw, Copy, SharedRequest, SharedMark,
                   TexturePrepare, TextureCommit, SharedUpload, Count };
struct Entry { uint64_t cycles = 0, calls = 0; double us = 0; };
inline thread_local std::array<Entry, size_t(Phase::Count)> entries;
inline thread_local uint64_t frame = 0;
inline thread_local uint64_t upload_bytes = 0;
inline void AddUploadBytes(uint64_t bytes) {
  if (REXCVAR_GET(d3d12_profile_cpu_work)) upload_bytes += bytes;
}
inline uint64_t Cycles() {
  ULONG64 result = 0;
  QueryThreadCycleTime(GetCurrentThread(), &result);
  return result;
}
class DrawScope {
 public:
  explicit DrawScope(Phase phase = Phase::Setup)
      : enabled_(REXCVAR_GET(d3d12_profile_cpu_work)), phase_(phase) {
    if (enabled_) { start_ = std::chrono::steady_clock::now(); cycles_ = Cycles(); }
  }
  ~DrawScope() { if (enabled_) Record(); }
  void Next(Phase phase) {
    if (!enabled_) return;
    Record();
    phase_ = phase;
    start_ = std::chrono::steady_clock::now();
    cycles_ = Cycles();
  }
 private:
  void Record() {
    const auto cycles = Cycles();
    const auto end = std::chrono::steady_clock::now();
    auto& entry = entries[size_t(phase_)];
    entry.cycles += cycles - cycles_;
    entry.us += std::chrono::duration<double, std::micro>(end - start_).count();
    ++entry.calls;
  }
  bool enabled_;
  Phase phase_ = Phase::Setup;
  std::chrono::steady_clock::time_point start_;
  uint64_t cycles_ = 0;
};
inline void EndFrame() {
  if (!REXCVAR_GET(d3d12_profile_cpu_work)) return;
  const auto interval = uint64_t(REXCVAR_GET(d3d12_profile_cpu_interval));
  if (++frame % interval) return;
  constexpr const char* names[] = {"setup", "primitive", "render_targets", "pipeline",
      "textures", "fixed_state", "bindings", "residency", "draw", "copy",
      "subset_shared_request", "subset_shared_mark", "subset_texture_prepare", "subset_texture_commit",
      "subset_shared_upload"};
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];
    REXGPU_INFO("CPU_WORK frame={} phase={} calls={} cycles={} wall_us={:.3f} frames={}",
                frame, names[i], e.calls, e.cycles, e.us, interval);
  }
  entries = {};
  REXGPU_INFO("CPU_UPLOAD frame={} bytes={} frames={}", frame, upload_bytes, interval);
  upload_bytes = 0;
}
}  // namespace rex::graphics::d3d12::work_profile
#define REX_CPU_DRAW_SCOPE() work_profile::DrawScope cpu_work_scope
#define REX_CPU_DRAW_NEXT(name) cpu_work_scope.Next(work_profile::Phase::name)
#define REX_CPU_SUBSET_SCOPE(name) ::rex::graphics::d3d12::work_profile::DrawScope cpu_subset_scope( \
    ::rex::graphics::d3d12::work_profile::Phase::name)
#define REX_CPU_UPLOAD_BYTES(bytes) ::rex::graphics::d3d12::work_profile::AddUploadBytes(bytes)
#else
#define REX_CPU_DRAW_SCOPE() ((void)0)
#define REX_CPU_DRAW_NEXT(name) ((void)0)
#define REX_CPU_SUBSET_SCOPE(name) ((void)0)
#define REX_CPU_UPLOAD_BYTES(bytes) ((void)0)
#endif
