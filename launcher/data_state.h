#pragma once
#include "common/catalogue.h"

namespace cave::launcher {
inline bool MissingGameData(const Inspection& inspection) {
  // Optional content and runtime availability do not decide whether a game's
  // original data is present. A pending inspection has no result to dim yet.
  return inspection.state!="checking" && inspection.data_state==DataState::Missing;
}
}
