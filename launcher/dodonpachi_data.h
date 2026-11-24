#pragma once
#include "common/launch.h"

namespace cave::launcher {
fs::path FindDoDonPachiZip(const fs::path& data);
void ConfigureDoDonPachi(LaunchSpec& launch);
}
