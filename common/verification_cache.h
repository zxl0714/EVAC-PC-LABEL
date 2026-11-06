#pragma once
#include "common/catalogue.h"
#include <map>

namespace cave {
// Reuse file digests, then compare them with the current manifest as usual.
// File identity and NTFS/ReFS change time invalidate entries across replacement,
// in-place edits (including restored modification times), and volume changes.
class VerificationCache {
 public:
  explicit VerificationCache(const fs::path& root);
  ~VerificationCache();
  VerificationCache(const VerificationCache&) = delete;
  VerificationCache& operator=(const VerificationCache&) = delete;
  std::string Digest(const fs::path& file);
  std::string DigestRange(const fs::path& file,uint64_t offset,uint64_t bytes);
  void Flush() noexcept;
  uint64_t hits{}, reads{}, bytes_read{};
 private:
  fs::path directory_, path_;
  std::map<std::string,std::string> entries_, pending_;
};
std::string FileDigest(const fs::path& file,VerificationCache* cache);
std::string FileDigestRange(const fs::path& file,uint64_t offset,uint64_t bytes,VerificationCache* cache);
}
