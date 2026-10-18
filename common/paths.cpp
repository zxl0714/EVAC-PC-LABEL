#include "common/paths.h"
#include <Windows.h>
#include <array>
#include <stdexcept>

namespace cave {
fs::path FromUtf8(std::string_view text) {
  if (text.empty()) return {};
  if (text.size() > 32767 || text.find('\0') != text.npos) throw std::runtime_error("Invalid UTF-8 path");
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
  if (!count) throw std::runtime_error("Invalid UTF-8 text");
  std::wstring value(count, 0);
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), value.data(), count) != count)
    throw std::runtime_error("UTF-8 conversion failed");
  return fs::path(value);
}
std::string Utf8(const fs::path& path) {
  const auto& text = path.native();
  if (text.empty()) return {};
  int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  if (!count) throw std::runtime_error("Invalid Unicode path");
  std::string value(count, 0);
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), value.data(), count, nullptr, nullptr) != count)
    throw std::runtime_error("Unicode conversion failed");
  return value;
}
fs::path Resolve(const fs::path& root, const fs::path& value) {
  if (value.empty()) throw std::runtime_error("Empty path");
  // Do not accept drive-relative C:folder or current-drive-rooted \folder.
  if (value.has_root_path() && !value.is_absolute()) throw std::runtime_error("Use an absolute or root-relative path");
  return fs::weakly_canonical(fs::absolute(value.is_absolute() ? value : root / value));
}
bool Within(const fs::path& child, const fs::path& parent) {
  auto a = child.begin(), b = parent.begin();
  for (; b != parent.end(); ++a, ++b) {
    if (a == child.end()) return false;
    const auto& x = a->native(); const auto& y = b->native();
    if (CompareStringOrdinal(x.data(), int(x.size()), y.data(), int(y.size()), TRUE) != CSTR_EQUAL) return false;
  }
  return true;
}
fs::path DataChild(const fs::path& root, std::string_view relative) {
  auto name = FromUtf8(relative);
  if (name.empty() || name.has_root_path() || relative.find(':') != relative.npos)
    throw std::runtime_error("Invalid manifest path");
  for (const auto& part : name) if (part == ".." || part == ".") throw std::runtime_error("Invalid manifest path component");
  auto result = Resolve(root, name);
  if (!Within(result, root) || result == root) throw std::runtime_error("File link escapes its data/component root");
  return result;
}
GamePaths Paths(const fs::path& root, std::string_view game, const fs::path& data, std::string_view profile) {
  auto token = [](std::string_view value) {
    return !value.empty() && value.size() <= 64 && value.find_first_not_of(
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == value.npos;
  };
  if (!token(game) || !token(profile)) throw std::runtime_error("Invalid game or profile ID");
  auto base = fs::weakly_canonical(fs::absolute(root));
  const auto id = FromUtf8(game);
  return {base, Resolve(base, data), Resolve(base, fs::path("prepared") / id),
          Resolve(base, fs::path("user-data") / id / FromUtf8(profile)),
          Resolve(base, fs::path("logs") / id), Resolve(base, fs::path("cache") / id / "contract-1")};
}
void ValidateOutputs(const GamePaths& paths) {
  const std::array outputs{paths.prepared, paths.profile, paths.logs, paths.cache};
  for (const auto& output : outputs) {
    if (!Within(output, paths.root) || output == paths.root)
      throw std::runtime_error("Writable path redirects outside collection root");
    if (Within(output, paths.data) || Within(paths.data, output))
      throw std::runtime_error("Writable path overlaps read-only game data");
  }
  for (size_t i = 0; i < outputs.size(); ++i) for (size_t j = i + 1; j < outputs.size(); ++j)
    if (Within(outputs[i], outputs[j]) || Within(outputs[j], outputs[i]))
      throw std::runtime_error("Writable paths overlap");
}
}  // namespace cave
