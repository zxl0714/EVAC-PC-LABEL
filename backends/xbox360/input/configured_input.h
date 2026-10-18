#pragma once
#include "config/settings.h"
#include <memory>
#include <rex/system/interfaces/input.h>
namespace espgaluda2 {
std::unique_ptr<rex::system::IInputSystem> CreateConfiguredInput(const Settings& settings, bool tool_mode);
}
