#pragma once
#include "cache_memory.h"
#include "host_services.h"
#include <cstddef>
#include <cstdint>

namespace Ketsui::GpuCore {
// This SDK is built as one clang-cl/MSVC-runtime pair. Shader, Vulkan and
// register-layout classes never cross the DLL boundary.
inline constexpr std::uint32_t RuntimeApiVersion = 5;
struct RuntimeError {
  char message[2048]{};
};
struct RuntimeStats {
  std::uint64_t draws{}, dispatches{};
  std::uint32_t validation_errors{};
};
struct RuntimeRegisterLayout {
  std::uint32_t bank_words, vs_program, ps_program, color_buffers;
  std::uint32_t color_target_mask, color_shader_mask, color_control,
      color_export_format;
  std::uint32_t shader_pos_format, vs_output_control, stage_enable,
      primitive_type;
  std::uint32_t num_indices, num_instances, viewports, viewport_control;
  std::uint32_t screen_scissor, window_scissor, generic_scissor,
      viewport_depths;
};
struct RuntimeDisplay {
  std::uint64_t address;
  std::uint32_t width, height, pitch, pixel_format, tiling_mode;
};
enum class RuntimePresentStatus : std::uint32_t {
  Completed,
  ZeroExtent,
  OutOfDate
};
struct RuntimePresentResult {
  RuntimePresentStatus status{};
  std::uint32_t width{}, height{};
  std::uint64_t tick{}, completed_presents{};
};
struct RuntimeApi {
  std::uint32_t version{}, bytes{};
  bool (*create)(const HostSettings *, CacheMemoryBackend *, void **,
                 RuntimeError *) noexcept;
  bool (*destroy)(void *, RuntimeError *) noexcept;
  bool (*map)(void *, std::uint64_t, std::uint64_t, RuntimeError *) noexcept;
  bool (*access)(void *, std::uint64_t, std::uint64_t, bool,
                 RuntimeError *) noexcept;
  bool (*unmap)(void *, std::uint64_t, std::uint64_t, RuntimeError *) noexcept;
  bool (*drain)(void *, RuntimeError *) noexcept;
  bool (*registers)(void *, std::uint32_t, const std::uint32_t *, std::uint32_t,
                    RuntimeError *) noexcept;
  bool (*extent)(void *, std::uint32_t, std::uint32_t, std::uint32_t,
                 RuntimeError *) noexcept;
  bool (*draw)(void *, bool, std::uint32_t, RuntimeError *) noexcept;
  bool (*flush)(void *, std::uint64_t *, RuntimeError *) noexcept;
  bool (*read_color)(void *, std::uint64_t, std::uint32_t, std::uint32_t,
                     void *, std::uint64_t, RuntimeError *) noexcept;
  RuntimeStats (*stats)() noexcept;
  RuntimeRegisterLayout (*layout)() noexcept;
  bool (*present)(void *, const RuntimeDisplay *, RuntimePresentResult *,
                  RuntimeError *) noexcept;
  bool (*set_window)(void *, void *, RuntimeError *) noexcept;
  bool (*clear)(void *, RuntimeError *) noexcept;
};
using GetRuntimeApi = bool (*)(std::uint32_t, std::uint32_t,
                               RuntimeApi *) noexcept;
} // namespace Ketsui::GpuCore
