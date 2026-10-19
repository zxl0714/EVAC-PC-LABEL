#pragma once
#include <cstdint>

enum GpuReadbacksMode : int { Disabled, Relaxed, Precise };

struct GpuHostSettingsView {
  bool IsHdrAllowed() const;
  bool IsVkValidationCoreEnabled() const;
  bool IsVkValidationSyncEnabled() const;
  bool IsVkValidationGpuEnabled() const;
  bool IsReadbackLinearImagesEnabled() const;
  std::uint32_t GetReadbacksMode() const;
  bool IsDirectMemoryAccessEnabled() const;
  bool IsInlineFetchShader() const;
  bool IsDumpShaders() const;
  bool IsVkHostMarkersEnabled() const;
  bool IsVkGuestMarkersEnabled() const;
};

inline constexpr GpuHostSettingsView EmulatorSettings{};
