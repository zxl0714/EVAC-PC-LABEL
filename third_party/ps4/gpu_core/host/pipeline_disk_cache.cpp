#include "pipeline_disk_cache.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Ketsui::GpuCore {
namespace {
// Check the full payload before passing opaque driver data back to Vulkan.
constexpr std::array<std::uint8_t, 8> Magic{'E', 'V', 'V', 'K',
                                            'C', '0', '0', '1'};
constexpr std::size_t EnvelopeBytes = 24;
std::uint64_t Checksum(std::span<const std::uint8_t> bytes) {
  std::uint64_t value = 14695981039346656037ull;
  for (auto byte : bytes)
    value = (value ^ byte) * 1099511628211ull;
  return value;
}
} // namespace
std::filesystem::path PipelineDiskPath(const std::filesystem::path &root,
                                       const PipelineDiskIdentity &id) {
  if (root.empty())
    return {};
  std::ostringstream name;
  name << "vk-" << std::hex << id.vendor << '-' << id.device << '-';
  for (auto byte : id.uuid)
    name << std::setw(2) << std::setfill('0') << unsigned(byte);
  name << ".bin";
  return root / name.str();
}
bool ValidPipelineDiskData(std::span<const std::uint8_t> data,
                           const PipelineDiskIdentity &id) noexcept {
  if (data.size() < 32 || data.size() > MaxPipelineDiskBytes)
    return false;
  std::array<std::uint32_t, 4> header{};
  std::memcpy(header.data(), data.data(), 16);
  return header[0] >= 32 && header[0] <= data.size() && header[1] == 1 &&
         header[2] == id.vendor && header[3] == id.device &&
         std::memcmp(data.data() + 16, id.uuid.data(), 16) == 0;
}
std::vector<std::uint8_t>
ReadPipelineDisk(const std::filesystem::path &path,
                 const PipelineDiskIdentity &id) noexcept {
  try {
    if (path.empty())
      return {};
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size < EnvelopeBytes + 32 ||
        size > MaxPipelineDiskBytes + EnvelopeBytes)
      return {};
    std::ifstream file(path, std::ios::binary);
    std::array<std::uint8_t, EnvelopeBytes> header{};
    if (!file.read(reinterpret_cast<char *>(header.data()), header.size()) ||
        !std::equal(Magic.begin(), Magic.end(), header.begin()))
      return {};
    std::uint64_t length{}, checksum{};
    std::memcpy(&length, header.data() + 8, 8);
    std::memcpy(&checksum, header.data() + 16, 8);
    if (length != size - EnvelopeBytes)
      return {};
    std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
    if (!file.read(reinterpret_cast<char *>(data.data()),
                   static_cast<std::streamsize>(data.size())) ||
        file.peek() != std::char_traits<char>::eof() ||
        Checksum(data) != checksum || !ValidPipelineDiskData(data, id))
      return {};
    return data;
  } catch (...) {
    return {};
  }
}
bool WritePipelineDisk(const std::filesystem::path &path,
                       std::span<const std::uint8_t> data,
                       const PipelineDiskIdentity &id) noexcept {
  std::filesystem::path temporary;
  try {
    if (path.empty() || !ValidPipelineDiskData(data, id))
      return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
      return false;
    static std::atomic<unsigned> sequence{};
    temporary = path;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(GetTickCount64()) + L"-" +
                 std::to_wstring(sequence.fetch_add(1));
    {
      std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
      std::array<std::uint8_t, EnvelopeBytes> header{};
      std::copy(Magic.begin(), Magic.end(), header.begin());
      const std::uint64_t length = data.size(), checksum = Checksum(data);
      std::memcpy(header.data() + 8, &length, 8);
      std::memcpy(header.data() + 16, &checksum, 8);
      file.write(reinterpret_cast<const char *>(header.data()), header.size());
      file.write(reinterpret_cast<const char *>(data.data()),
                 static_cast<std::streamsize>(data.size()));
      file.flush();
      if (!file)
        throw std::runtime_error("Pipeline cache write failed");
    }
    if (MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      return true;
  } catch (...) {
  }
  if (!temporary.empty()) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
  }
  return false;
}
} // namespace Ketsui::GpuCore
