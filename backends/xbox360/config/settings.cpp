#include "settings.h"
namespace espgaluda2 {
Settings ParseSettings(std::string_view text) { return cave::ParseSettings(text,"espgaluda2"); }
Settings LoadSettings(const std::filesystem::path& path) { return cave::LoadSettings(path,"espgaluda2"); }
std::string ReadSettingsText(const std::filesystem::path& path) { return cave::Ini::ReadText(path); }
}
