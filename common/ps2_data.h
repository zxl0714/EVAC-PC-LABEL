#pragma once
#include "common/catalogue.h"

namespace cave {
// The manifest names the original boot entry and a logical disc.iso identity.
// The player's ISO filename is preserved; no extracted files are required.
void InspectPs2Disc(const Game& game,const fs::path& directory,CheckLevel level,Inspection& result,VerificationCache* cache=nullptr);
}
