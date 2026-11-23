#pragma once
#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>

namespace cave::ps2 {
// Exposes 2048-byte user sectors at the same offsets for ISO and CHD images.
class DiscImage final : public std::istream {
 public:
  explicit DiscImage(const std::filesystem::path& path);
  ~DiscImage() override;
  uint64_t Size() const;
  static bool IsChd(const std::filesystem::path& path);
 private:
  class Buffer;
  std::unique_ptr<Buffer> buffer_;
};
}
