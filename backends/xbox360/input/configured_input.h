#pragma once
#include "common/settings.h"
#include <memory>
#include <rex/system/interfaces/input.h>
namespace cave::xbox360 {
std::unique_ptr<rex::system::IInputSystem> CreateConfiguredInput(const Settings& settings, bool tool_mode);
}
