#pragma once
#include "common/catalogue.h"
#include <functional>

namespace cave {
struct CopyResult {
  fs::path directory;
  uint64_t files{},bytes{};
  fs::path guide_backup;
};
using CopyProgress=std::function<void(uint64_t,uint64_t)>;
inline bool PreservesXArrangeOnImport(std::string_view game) {
  return game=="ketsui" || game=="daioujou";
}
// Copies supported data and preserves placement guides and supported X Arrange files.
// Publish the verified directory and update config without overwriting existing data.
// Counts describe source data.
CopyResult CopyDataToCollection(const fs::path& root,const Game& game,const fs::path& source,
                                CopyProgress progress={},std::function<bool()> cancelled={});
}
