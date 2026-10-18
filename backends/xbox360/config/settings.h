#pragma once
#include "common/settings.h"

// Transitional adapter for the migrated G2 host/input API. All parsing and
// defaults live in common and consume the collection's one configuration.
namespace espgaluda2 {
using cave::Settings;
using cave::Binding;
using cave::kButtons;
using cave::KeyCode;
using cave::ValidUtf8;
Settings ParseSettings(std::string_view text);
Settings LoadSettings(const std::filesystem::path& path);
std::string ReadSettingsText(const std::filesystem::path& path);
}
