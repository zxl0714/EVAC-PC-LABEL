#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <fmt/format.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/thread.h"
#include "core/emulator_settings.h"
#include "host_services.h"
#include "gpu_metrics.h"
#include "imgui/renderer/imgui_core.h"
#include "imgui/renderer/texture_manager.h"

namespace {
Ketsui::GpuCore::HostSettings settings;
std::filesystem::path shader_directory;
std::atomic<std::uint32_t> validation_errors{0};
std::atomic<std::uint32_t> overlay_submissions{0};
std::atomic<std::uint64_t> draws{0}, dispatches{0};
std::mutex log_mutex;
} // namespace

namespace Ketsui::GpuCore {
void ConfigureHost(HostSettings supplied) {
  if (!supplied.log_directory.is_absolute() ||
      !std::filesystem::is_directory(supplied.log_directory)) {
    throw std::invalid_argument(
        "GPU host needs an existing absolute log directory");
  }
  settings = std::move(supplied);
  shader_directory = settings.log_directory / "shader";
}

std::uint32_t ValidationErrors() { return validation_errors.load(); }
std::uint32_t OverlaySubmissions() { return overlay_submissions.load(); }
bool IsNeoMode() { return settings.neo; }
bool HasPresentationSupport() { return settings.native_window != nullptr || settings.presentation_support; }
void TimingLog(const char* format, ...) noexcept {
  char buffer[2048];
  va_list args; va_start(args,format);
  const auto size=std::vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
  if (size <= 0 || static_cast<std::size_t>(size) >= sizeof(buffer)) return;
  if (settings.diagnostic_sink) (void)settings.diagnostic_sink(buffer,static_cast<std::size_t>(size));
  else (void)std::fwrite(buffer,1,static_cast<std::size_t>(size),stdout);
}
void RecordDraw() { ++draws; }
void RecordDispatch() { ++dispatches; }
std::uint64_t DrawCount() { return draws.load(); }
std::uint64_t DispatchCount() { return dispatches.load(); }
} // namespace Ketsui::GpuCore

bool GpuHostSettingsView::IsHdrAllowed() const { return settings.hdr; }
bool GpuHostSettingsView::IsVkValidationCoreEnabled() const {
  return settings.validation_core;
}
bool GpuHostSettingsView::IsVkValidationSyncEnabled() const {
  return settings.validation_sync;
}
bool GpuHostSettingsView::IsVkValidationGpuEnabled() const {
  return settings.validation_gpu;
}
bool GpuHostSettingsView::IsReadbackLinearImagesEnabled() const {
  return settings.readback_linear_images;
}
std::uint32_t GpuHostSettingsView::GetReadbacksMode() const {
  return settings.readbacks_mode;
}
bool GpuHostSettingsView::IsDirectMemoryAccessEnabled() const {
  return settings.direct_memory_access;
}
bool GpuHostSettingsView::IsInlineFetchShader() const {
  return settings.inline_fetch_shader;
}
bool GpuHostSettingsView::IsDumpShaders() const {
  return settings.dump_shaders;
}
bool GpuHostSettingsView::IsVkHostMarkersEnabled() const { return settings.host_markers; }
bool GpuHostSettingsView::IsVkGuestMarkersEnabled() const { return settings.guest_markers; }

namespace Common::Log {
std::array<Level, NUM_LOG_CLASSES> g_class_levels = [] {
  std::array<Level, NUM_LOG_CLASSES> result{};
  result.fill(Level::Info);
  return result;
}();

void VLog(Class log_class, Level level, const char *file, int line,
          const char *, fmt::string_view format, fmt::format_args args) {
  if (log_class == Class::Render_Vulkan && level >= Level::Error) {
    validation_errors.fetch_add(1);
  }
  std::scoped_lock lock{log_mutex};
  auto message = fmt::vformat(format, args);
  if (message.size() > 16384) {
    message.resize(16384);
  }
  std::fprintf(stderr, "gpu_core[%u] %s:%d %s\n", static_cast<unsigned>(level),
               file, line, message.c_str());
}
} // namespace Common::Log

void assert_fail_impl() { std::abort(); }
[[noreturn]] void unreachable_impl() { std::abort(); }

namespace Common::FS {
std::string PathToUTF8String(const std::filesystem::path &path) {
  const auto bytes = path.u8string();
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
const std::filesystem::path &GetUserPath(PathType type) {
  if (settings.log_directory.empty()) {
    throw std::logic_error("GPU host path has not been configured");
  }
  if (type == PathType::LogDir) {
    return settings.log_directory;
  }
  if (type == PathType::ShaderDir) {
    return shader_directory;
  }
  throw std::logic_error("GPU host path type is not configured");
}
std::string GetUserPathString(PathType type) {
  if (type != PathType::LogDir || settings.log_directory.empty()) {
    throw std::logic_error("GPU host path has not been configured");
  }
  const auto utf8 = settings.log_directory.u8string();
  return {reinterpret_cast<const char *>(utf8.data()), utf8.size()};
}
} // namespace Common::FS

namespace Common {
void SetCurrentThreadName(const char *name) {
  const int length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
  if (!length) {
    throw std::runtime_error("Invalid GPU worker thread name");
  }
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide.data(),
                           length) ||
      FAILED(SetThreadDescription(GetCurrentThread(), wide.c_str()))) {
    throw std::runtime_error("Could not name GPU worker thread");
  }
}
} // namespace Common

// No overlay renderer or overlay GPU resources are created by this component.
namespace ImGui::Core {
void Shutdown(vk::Device) {}
namespace TextureManager {
void Submit() { overlay_submissions.fetch_add(1); }
} // namespace TextureManager
} // namespace ImGui::Core
