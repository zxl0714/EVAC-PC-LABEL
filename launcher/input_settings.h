#pragma once
#include "common/catalogue.h"
#include "launcher/strings.h"
#include <Windows.h>

namespace cave::launcher::controls {
bool Show(HWND owner,const fs::path& root,const Game& game,Language language);
}
