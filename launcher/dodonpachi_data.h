#pragma once

#include "common/paths.h"

namespace cave::launcher {
// Return the selected archive, a directory of loose chips, or an empty path.
fs::path FindDoDonPachiSource(const fs::path& data);

// Prepare the upstream program's ROM directory and choose the first edition for
// this source. A subsequent launch preserves the player's in-game VERSION choice.
void ConfigureDoDonPachi(const fs::path& source, const fs::path& prepared,
                         const fs::path& program, const fs::path& settings);
}
