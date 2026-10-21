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
// Copies supported data, never moves its source. The default target must be
// absent, empty, or contain only a placement guide. Preserve that guide and
// publish/update config only after verification. Counts describe source data.
CopyResult CopyDataToCollection(const fs::path& root,const Game& game,const fs::path& source,
                                CopyProgress progress={},std::function<bool()> cancelled={});
}
