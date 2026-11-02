#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Ketsui::GpuCore {
struct PipelineDiskIdentity {
  std::uint32_t vendor{}, device{};
  std::array<std::uint8_t, 16> uuid{};
};
inline constexpr std::size_t MaxPipelineDiskBytes = 128u * 1024u * 1024u;
std::filesystem::path PipelineDiskPath(const std::filesystem::path &,
                                       const PipelineDiskIdentity &);
bool ValidPipelineDiskData(std::span<const std::uint8_t>,
                           const PipelineDiskIdentity &) noexcept;
std::vector<std::uint8_t>
ReadPipelineDisk(const std::filesystem::path &,
                 const PipelineDiskIdentity &) noexcept;
bool WritePipelineDisk(const std::filesystem::path &,
                       std::span<const std::uint8_t>,
                       const PipelineDiskIdentity &) noexcept;
} // namespace Ketsui::GpuCore
