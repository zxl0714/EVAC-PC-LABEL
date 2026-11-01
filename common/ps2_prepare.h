#pragma once
#include "common/paths.h"

namespace cave {
struct Ps2PreparedFile {
  std::string_view disc_path,path;
  uint64_t bytes;
  std::string_view sha256;
};
// Extract a small reviewed boot/IOP set into the prepared cache. Large resources
// remain in the original read-only ISO and retain their original disc LBAs.
fs::path PreparePs2Disc(const GamePaths& paths,const fs::path& image,std::string_view disc_sha256,
                        std::span<const Ps2PreparedFile> files);
}
