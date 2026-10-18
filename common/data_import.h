#pragma once
#include "common/catalogue.h"
#include <functional>

namespace cave {
struct CopyResult {
  fs::path directory;
  uint64_t files{},bytes{};
};
using CopyProgress=std::function<void(uint64_t,uint64_t)>;
// Copies supported data, never moves its source. The default target must be
// absent or empty; publish and update config.ini only after verification.
CopyResult CopyDataToCollection(const fs::path& root,const Game& game,const fs::path& source,
                                CopyProgress progress={},std::function<bool()> cancelled={});
}
