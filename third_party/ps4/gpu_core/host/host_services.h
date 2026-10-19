#pragma once

#include <cstdint>
#include <filesystem>

namespace Ketsui::GpuCore {

struct HostSettings {
  bool validation_core = true;
  bool validation_sync = true;
  bool validation_gpu = false;
  bool hdr = false;
  bool neo = false;
  bool direct_memory_access = false;
  bool inline_fetch_shader = false;
  bool dump_shaders = false;
  bool host_markers = false;
  bool guest_markers = false;
  bool readback_linear_images = true;
  std::uint32_t readbacks_mode = 2;
  std::filesystem::path log_directory;
  void *native_window = nullptr;
  bool presentation_support = false;
};

void ConfigureHost(HostSettings settings);
std::uint32_t ValidationErrors();
std::uint32_t OverlaySubmissions();
bool IsNeoMode();
bool HasPresentationSupport();

} // namespace Ketsui::GpuCore
