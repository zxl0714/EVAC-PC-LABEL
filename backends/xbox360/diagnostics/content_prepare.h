#pragma once
#include "common/paths.h"

namespace cave::xbox360 {
// Materialize only verified resources in an owned, read-only guest mount.
// Xbox installation metadata belongs here, never in the player's extraction.
fs::path PrepareContent(const Game& game,const GamePaths& paths,const fs::path& source,
                        const std::vector<ContentInspection>& inspected);
}
