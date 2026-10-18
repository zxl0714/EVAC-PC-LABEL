#pragma once
#include "common/catalogue.h"
#include <map>
#include <optional>

namespace cave {
struct IniValue { std::string text; size_t line; };
class Ini {
 public:
  static Ini Parse(std::string_view text);
  static Ini Load(const fs::path& path);
  static std::string ReadText(const fs::path& path);
  const IniValue* Get(std::string_view section, std::string_view key) const;
  const auto& Values() const { return values_; }
  fs::path DataDirectory(const Game& game) const;
 private:
  std::map<std::pair<std::string, std::string>, IniValue> values_;
};
}  // namespace cave
