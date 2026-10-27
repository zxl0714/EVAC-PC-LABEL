#pragma once
#include "common/catalogue.h"
#include <memory>

namespace cave {
// Bounded, read-only LIVE/PIRS reader. Extraction is a separate operation
// into a new cache directory; inspection never writes to the player's source.
struct StfsFile {
  std::string path, sha256;
  uint32_t bytes{};
  std::vector<uint32_t> offsets;
};
class StfsPackage {
 public:
  explicit StfsPackage(const fs::path& source, bool verify, uint32_t content_type = 2);
  ~StfsPackage();
  uint32_t TitleId() const;
  const std::vector<StfsFile>& Files() const;
  void Extract(const fs::path& output) const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
// Missing means a different title/package, not a damaged matching package.
ContentInspection InspectStfs(const ContentPackage& package, const fs::path& file, CheckLevel level);
}
