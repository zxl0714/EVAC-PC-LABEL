#include "common/ini.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cave {
namespace {
std::string Lower(std::string_view input) {
  std::string value(input);
  for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return value;
}
std::string_view Trim(std::string_view value) {
  auto first = value.find_first_not_of(" \t\r");
  return first == value.npos ? std::string_view{} : value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
}  // namespace
Ini Ini::Parse(std::string_view text) {
  if (text.size() > 1024 * 1024) throw std::runtime_error("config.ini exceeds 1 MiB");
  if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
  // Validate the complete document, including comments, as UTF-8.
  if (text.find('\0') != text.npos) throw std::runtime_error("NUL in config.ini");
  // Path conversion has a bounded input; validate each line separately below.
  Ini ini;
  std::istringstream stream{std::string(text)};
  std::string line, section;
  size_t number = 0;
  while (std::getline(stream, line)) {
    ++number;
    try {
      (void)FromUtf8(line);
      auto value = Trim(line);
      if (value.empty() || value.front() == ';') continue;
      if (value.front() == '[') {
        if (value.back() != ']' || value.size() < 3) throw std::runtime_error("Invalid section");
        section = Lower(Trim(value.substr(1, value.size() - 2)));
        if (section.empty() || section.find_first_of("[]=") != section.npos) throw std::runtime_error("Invalid section");
        continue;
      }
      auto equal = value.find('=');
      if (section.empty() || equal == value.npos || Trim(value.substr(0, equal)).empty())
        throw std::runtime_error("Expected [section] and key = value");
      auto key = Lower(Trim(value.substr(0, equal)));
      if (!ini.values_.emplace(std::pair{section, key}, IniValue{std::string(Trim(value.substr(equal + 1))), number}).second)
        throw std::runtime_error("Duplicate [" + section + "] " + key);
    } catch (const std::exception& error) {
      throw std::runtime_error("config.ini line " + std::to_string(number) + ": " + error.what());
    }
  }
  auto version = ini.Get("collection", "config_version");
  if (version && version->text != "1") throw std::runtime_error("Unsupported [collection] config_version");
  return ini;
}
std::string Ini::ReadText(const fs::path& path) {
  if (!fs::exists(path)) return {};
  if (!fs::is_regular_file(path) || fs::file_size(path) > 1024 * 1024) throw std::runtime_error("Invalid config.ini file");
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot read config.ini");
  std::string text{std::istreambuf_iterator<char>(file), {}};
  if (file.bad()) throw std::runtime_error("Failed reading config.ini");
  return text;
}
Ini Ini::Load(const fs::path& path) { return Parse(ReadText(path)); }
const IniValue* Ini::Get(std::string_view section, std::string_view key) const {
  auto found = values_.find({Lower(section), Lower(key)});
  return found == values_.end() ? nullptr : &found->second;
}
fs::path Ini::DataDirectory(const Game& game) const {
  const auto section = "game." + std::string(game.id);
  auto value = Get(section, "data_dir");
  if (value && value->text.empty()) throw std::runtime_error("config.ini line " + std::to_string(value->line) + ": [" + section + "] data_dir is empty");
  return FromUtf8(value ? value->text : game.data_dir);
}
}  // namespace cave
